#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define FOPEN_BINARY "bS"
#else
#define FOPEN_BINARY
#endif

/* report drops as with resync */
#ifndef REPORT_DROP
#define REPORT_DROP 1
#endif

typedef struct {

	int           pid;
	int           last_continuity_counter;

	int64_t       first;
	int64_t       total;
	int64_t       error;
	int64_t       drop;
	int64_t       scrambling;

	unsigned char last_packet[188];
	int           duplicate_count;
	int           report_drop;

} TS_STATUS;

typedef struct {

	int64_t       miss;
	int64_t       sync;

	int64_t       drop_count;

	short         drop_pid[4];
	int64_t       drop_pos[4];

	int           last_mjd, mjd;
	int           last_h, h;
	int           last_m, m;
	int           last_s, s;

} RESYNC_REPORT;

typedef struct {
	int           sync;
	int           transport_error_indicator;
	int           payload_unit_start_indicator;
	int           transport_priority;
	int           pid;
	int           transport_scrambling_control;
	int           adaptation_field_control;
	int           continuity_counter;
} TS_HEADER;

typedef struct {

	int           adaptation_field_length;

	int           discontinuity_counter;
	int           random_access_indicator;
	int           elementary_stream_priority_indicator;
	int           pcr_flag;
	int           opcr_flag;
	int           splicing_point_flag;
	int           transport_private_data_flag;
	int           adaptation_field_extension_flag;

	int64_t       program_clock_reference;
	int64_t       original_program_clock_reference;

	int           splice_countdown;

	int           transport_private_data_length;

	int           adaptation_field_extension_length;
	int           ltw_flag;
	int           piecewise_rate_flag;
	int           seamless_splice_flag;
	int           ltw_valid_flag;
	int           ltw_offset;
	int           piecewise_rate;
	int           splice_type;
	int64_t       dts_next_au;

} ADAPTATION_FIELD;

static void show_usage();

static void tsdump(const char *path);
static void tsselect(const char *src, const char *dst, const unsigned char *pid);
static int select_unit_size(unsigned char *head, unsigned char *tail);
static unsigned char *resync(unsigned char *head, unsigned char *tail, int unit_size);
static unsigned char *resync_force(unsigned char *head, unsigned char *tail, int unit_size);
static void extract_ts_header(TS_HEADER *dst, unsigned char *packet);
static void extract_adaptation_field(ADAPTATION_FIELD *dst, unsigned char *data);
static int check_unit_invert(unsigned char *head, unsigned char *tail);

static void add_drop_info(RESYNC_REPORT *report, int count, int pid, int64_t pos);
static void print_resync_report(RESYNC_REPORT *report, int count);
static void set_resync_report_last_time(RESYNC_REPORT *report, int mjd, int h, int m, int s);
static void update_resync_report_time(RESYNC_REPORT *report, int count, int mjd, int h, int m, int s);
static void mjd_to_ymd(int mjd, int *y, int *m, int *d);

static int find_packet_time_data(unsigned char **time_data, const TS_HEADER *hdr, unsigned char *packet);
static void show_tdt_or_tot(TS_HEADER *hdr, unsigned char *packet, int64_t pos);

int main(int argc, char **argv)
{
	if(argc < 2){
		show_usage();
		exit(EXIT_FAILURE);
	}

	if(argc == 2){
		tsdump(argv[1]);
	}else{
		int i,n;
		int exclude;
		unsigned char pid[8192];

		memset(pid, 0, sizeof(pid));
		exclude = 0;
		for(i=3;i<argc;i++){

			if(argv[i][0] == '-'){
				if( (argv[i][1] == 'x') ||
				    (argv[i][1] == 'X') ){
					exclude = 1;
				}else{
					fprintf(stderr, "error - invalid option '-%c'\n", argv[i][1]);
					show_usage();
					exit(EXIT_FAILURE);
				}
				continue;
			}

			n = strtol(argv[i], NULL, 0);
			if( (n >= 0) && (n < 8192) ){
				pid[n] = 1;
			}
		}

		if(exclude){
			for(i=0;i<8192;i++){
				pid[i] = !pid[i];
			}
		}

		tsselect(argv[1], argv[2], pid);

	}

	return EXIT_SUCCESS;
}

static void show_usage()
{
	fprintf(stderr, "tsselect - MPEG-2 TS stream(pid) selector ver. 0.1.8\n");
	fprintf(stderr, "usage: tsselect src.m2t|- [dst.m2t|- pid [more pid ..]]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "ex: dump \"src.m2t\" TS information\n");
	fprintf(stderr, "  tsselect src.m2t\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "ex: remux \"src.m2t\" to \"dst.m2t\" which contains pid=0x1000 and pid=0x1001\n");
	fprintf(stderr, "  tsselect src.m2t dst.m2t 0x1000 0x1001\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "ex: remux \"src.m2t\" to \"dst.m2t\" exclude pid=0x0012(EIT) pid=0x0014(TOT)\n");
	fprintf(stderr, "  tsselect src.m2t dst.m2t -x 0x0012 0x0014\n");
	fprintf(stderr, "\n");
}

static void tsdump(const char *path)
{
	FILE *fp;

	int pid;
	int idx;
	int lcc;
	int i,m,n;
	int unit_size;
	int dropped;

	int64_t offset;
	int64_t total;

	TS_STATUS *stat;
	TS_HEADER  header;
	ADAPTATION_FIELD adapt;
	int last_mjd = 0;
	int last_h = 0;
	int last_m = 0;
	int last_s = 0;

	RESYNC_REPORT *resync_report, *rr;
	int resync_count;

	unsigned char *p;
	unsigned char *curr;
	unsigned char *tail;

	unsigned char buf[32768];

	fp = NULL;
	stat = NULL;

	resync_report = (RESYNC_REPORT *)calloc(1, sizeof(RESYNC_REPORT));
	resync_count = 0;

	if(path[0] == '-' && !path[1]){
		fp = stdin;
#ifdef _WIN32
		if(_setmode(_fileno(fp), _O_BINARY) < 0){
			fp = NULL;
		}
#endif
	}else{
		fp = fopen(path, "r" FOPEN_BINARY);
	}
	if(fp == NULL){
		fprintf(stderr, "error - failed on open(%s) [src]\n", path);
		goto LAST;
	}

	if(fp == stdin){
		total = 0;
	}else{
#ifdef _WIN32
		_fseeki64(fp, 0, SEEK_END);
		total = _ftelli64(fp);
		_fseeki64(fp, 0, SEEK_SET);
#else
		fseeko(fp, 0, SEEK_END);
		total = ftello(fp);
		fseeko(fp, 0, SEEK_SET);
#endif
	}

	stat = (TS_STATUS *)calloc(8192, sizeof(TS_STATUS));
	if(stat == NULL){
		fprintf(stderr, "error - failed on malloc(size=%d)\n", (int)(sizeof(TS_STATUS)*8192));
		goto LAST;
	}

	for(i=0;i<8192;i++){
		stat[i].pid = i;
		stat[i].last_continuity_counter = -1;
		stat[i].first = -1;
		stat[i].report_drop = REPORT_DROP;
	}

	offset = 0;
	idx = 0;
	n = fread(buf, 1, sizeof(buf) / 4, fp);

	unit_size = select_unit_size(buf, buf+n);
	if(unit_size < 188){
		fprintf(stderr, "error - failed on select_unit_size()\n");
		goto LAST;
	}

	do {
		curr = buf;
		tail = buf + n;
		while( (curr+unit_size) < tail ){
			if( (curr[0] != 0x47) || (curr[unit_size] != 0x47) ){
				if(resync_report){
					rr = resync_report;
					resync_report = (RESYNC_REPORT *)calloc(resync_count+1, sizeof(RESYNC_REPORT));
					if(resync_report){
						memcpy(resync_report, rr, sizeof(RESYNC_REPORT)*resync_count);
						resync_report[resync_count].miss = offset+(curr-buf);
					}
					free(rr);
				}
				p = resync(curr, tail, unit_size);
				if(p == NULL){
					break;
				}
				curr = p;
				if(resync_report){
					resync_report[resync_count].sync = offset+(curr-buf);
					set_resync_report_last_time(resync_report + resync_count, last_mjd, last_h, last_m, last_s);
				}
				resync_count += 1;
				for(i=0;i<8192;i++){
					stat[i].report_drop = 0;
				}
				if( (curr+unit_size) > tail ){
					break;
				}
			}

			extract_ts_header(&header, curr);
			if(header.adaptation_field_control & 2){
				extract_adaptation_field(&adapt, curr+4);
			}else{
				memset(&adapt, 0, sizeof(adapt));
			}

			pid = header.pid;
			if(pid == 0x14){
				/* maybe TDT or TOT */
				unsigned char *time_data;
				int table_id = find_packet_time_data(&time_data, &header, curr);
				if(table_id == 0x70 || table_id == 0x73){
					/* TDT or TOT */
					last_mjd = (time_data[0] << 8) | time_data[1];
					last_mjd = last_mjd < 15079 ? 15079 : last_mjd;
					last_h = ((time_data[2] >> 4) & 3) * 10 + (time_data[2] & 15);
					last_m = ((time_data[3] >> 4) & 7) * 10 + (time_data[3] & 15);
					last_s = ((time_data[4] >> 4) & 7) * 10 + (time_data[4] & 15);
					update_resync_report_time(resync_report, resync_count, last_mjd, last_h, last_m, last_s);
				}
			}
			if(stat[pid].first < 0){
				stat[pid].first = offset + (curr-buf);
			}
			lcc = stat[pid].last_continuity_counter;
			if( (lcc >= 0) && (adapt.discontinuity_counter == 0) ){
				dropped = 0;
				if( pid == 0x1fff ){
					// null packet - drop count has no mean
					// do nothing
				}else if( (header.adaptation_field_control & 0x01) == 0 ){
					// no payload : continuity_counter should not increment
					if(lcc != header.continuity_counter){
						dropped = 1;
					}
				}else if(lcc == header.continuity_counter){
					// has payload and same continuity_counter
					if(memcmp(stat[pid].last_packet, curr, 188) != 0){
						// non-duplicate packet
						dropped = 1;
					}
					stat[pid].duplicate_count += 1;
					if(stat[pid].duplicate_count > 1){
						// duplicate packet count exceeds limit (two)
						dropped = 1;
					}
				}else{
					m = (lcc + 1) & 0x0f;
					if(m != header.continuity_counter){
						dropped = 1;
					}
					stat[pid].duplicate_count = 0;
				}
				if(dropped){
					if(stat[pid].report_drop){
						if(resync_report){
							rr = resync_report;
							resync_report = (RESYNC_REPORT *)calloc(resync_count+1, sizeof(RESYNC_REPORT));
							if(resync_report){
								memcpy(resync_report, rr, sizeof(RESYNC_REPORT)*resync_count);
								resync_report[resync_count].miss =
									resync_report[resync_count].sync = offset+(curr-buf);
								set_resync_report_last_time(resync_report + resync_count, last_mjd, last_h, last_m, last_s);
							}
							free(rr);
						}
						resync_count += 1;
						for(i=0;i<8192;i++){
							stat[i].report_drop = 0;
						}
					}
					stat[pid].drop += 1;
					add_drop_info(resync_report, resync_count, pid, offset+(curr-buf));
				}
			}
			stat[pid].report_drop = REPORT_DROP;
			stat[pid].last_continuity_counter = header.continuity_counter;
			stat[pid].total += 1;
			if(header.transport_error_indicator != 0){
				stat[pid].error += 1;
			}
			memcpy(stat[pid].last_packet, curr, 188);
			if(header.transport_scrambling_control){
				stat[pid].scrambling += 1;
			}
			curr += unit_size;
		}

		offset += (curr-buf);

		if( (idx & 0x1f) == 0 ){
			if(total <= 0){
				fprintf(stderr, "\rprocessing: %5dM", (int)(offset/1024/1024));
			}else{
				n = (int)(10000*offset/total);
				fprintf(stderr, "\rprocessing: %2d.%02d%%", n/100, n%100);
			}
		}
		idx += 1;

		n = tail - curr;
		if(n > 0){
			memcpy(buf, curr, n);
		}
		m = fread(buf+n, 1, sizeof(buf)-n, fp);
		if(m < 1){
			break;
		}
		n += m;

	} while(n > unit_size);

	curr = buf;
	tail = buf + n;
	while( (curr+188) <= tail ){
		if(curr[0] != 0x47){
			if(resync_report){
				rr = resync_report;
				resync_report = (RESYNC_REPORT *)calloc(resync_count+1, sizeof(RESYNC_REPORT));
				if(resync_report){
					memcpy(resync_report, rr, sizeof(RESYNC_REPORT)*resync_count);
					resync_report[resync_count].miss = offset+(curr-buf);
				}
				free(rr);
			}
			p = resync_force(curr, tail, unit_size);
			if(p == NULL){
				break;
			}
			curr = p;
			if(resync_report){
				resync_report[resync_count].sync = offset+(curr-buf);
				set_resync_report_last_time(resync_report + resync_count, last_mjd, last_h, last_m, last_s);
			}
			resync_count += 1;
			for(i=0;i<8192;i++){
				stat[i].report_drop = 0;
			}
			if( (p+188) > tail ){
				break;
			}
		}
		extract_ts_header(&header, curr);
		if(header.adaptation_field_control & 2){
			extract_adaptation_field(&adapt, curr+4);
		}else{
			memset(&adapt, 0, sizeof(adapt));
		}
		pid = header.pid;
		if(stat[pid].first < 0){
			stat[pid].first = offset + (curr-buf);
		}
		lcc = stat[pid].last_continuity_counter;
		if( (lcc >= 0) && (adapt.discontinuity_counter == 0) ){
			dropped = 0;
			if( pid == 0x1fff ){
				// null packet - drop count has no mean
				// do nothing
			}else if( (header.adaptation_field_control & 0x01) == 0 ){
				// no payload : continuity_counter should not increment
				if(lcc != header.continuity_counter){
					dropped = 1;
				}
			}else if(lcc == header.continuity_counter){
				// has payload and same continuity_counter
				if(memcmp(stat[pid].last_packet, curr, 188) != 0){
					// non-duplicate packet
					dropped = 1;
				}
				stat[pid].duplicate_count += 1;
				if(stat[pid].duplicate_count > 1){
					// duplicate packet count exceeds limit (two)
					dropped = 1;
				}
			}else{
				m = (lcc + 1) & 0x0f;
				if(m != header.continuity_counter){
					dropped = 1;
				}
				stat[pid].duplicate_count = 0;
			}
			if(dropped){
				if(stat[pid].report_drop){
					if(resync_report){
						rr = resync_report;
						resync_report = (RESYNC_REPORT *)calloc(resync_count+1, sizeof(RESYNC_REPORT));
						if(resync_report){
							memcpy(resync_report, rr, sizeof(RESYNC_REPORT)*resync_count);
							resync_report[resync_count].miss =
								resync_report[resync_count].sync = offset+(curr-buf);
							set_resync_report_last_time(resync_report + resync_count, last_mjd, last_h, last_m, last_s);
						}
						free(rr);
					}
					resync_count += 1;
					for(i=0;i<8192;i++){
						stat[i].report_drop = 0;
					}
				}
				stat[pid].drop += 1;
				add_drop_info(resync_report, resync_count, pid, offset+(curr-buf));
			}
		}
		stat[pid].report_drop = REPORT_DROP;
		stat[pid].last_continuity_counter = header.continuity_counter;
		stat[pid].total += 1;
		if(header.transport_error_indicator != 0){
			stat[pid].error += 1;
		}
		memcpy(stat[pid].last_packet, curr, 188);
		if(header.transport_scrambling_control){
			stat[pid].scrambling += 1;
		}
		curr += unit_size;
	}

	fprintf(stderr, "\rprocessing: finish\n");
	fflush(stderr);

LAST:
	if(resync_count > 0){
		print_resync_report(resync_report, resync_count);
	}
	free(resync_report);

	if(stat){
		for(i=0;i<8192;i++){
			if(stat[i].total > 0){
				printf("pid=0x%04x, total=%8"PRId64", d=%3"PRId64", e=%3"PRId64", scrambling=%"PRId64", offset=%"PRId64"\n", i, stat[i].total, stat[i].drop, stat[i].error, stat[i].scrambling, stat[i].first);
			}
		}
		free(stat);
		stat = NULL;
	}

	if(fp != NULL){
		if(fp != stdin){
			fclose(fp);
		}
		fp = NULL;
	}
}

static void tsselect(const char *src, const char *dst, const unsigned char *pid)
{
	FILE *sfp, *dfp;

	int m,n;
	int idx;
	int unit_size;

	TS_HEADER header;

	int64_t offset;
	int64_t total;

	unsigned char *p;
	unsigned char *curr;
	unsigned char *tail;

	unsigned char buf[8192];

	sfp = NULL;
	dfp = NULL;

	if(src[0] == '-' && !src[1]){
		sfp = stdin;
#ifdef _WIN32
		if(_setmode(_fileno(sfp), _O_BINARY) < 0){
			sfp = NULL;
		}
#endif
	}else{
		sfp = fopen(src, "r" FOPEN_BINARY);
	}
	if(sfp == NULL){
		fprintf(stderr, "error - failed on open(%s) [src]\n", src);
		goto LAST;
	}

	if(dst[0] == '-' && !dst[1]){
		dfp = stdout;
#ifdef _WIN32
		if(_setmode(_fileno(dfp), _O_BINARY) < 0){
			dfp = NULL;
		}
#endif
	}else{
		dfp = fopen(dst, "w" FOPEN_BINARY);
	}
	if(dfp == NULL){
		fprintf(stderr, "error - failed on open(%s) [dst]\n", dst);
		goto LAST;
	}

	if(sfp == stdin){
		total = 0;
	}else{
#ifdef _WIN32
		_fseeki64(sfp, 0, SEEK_END);
		total = _ftelli64(sfp);
		_fseeki64(sfp, 0, SEEK_SET);
#else
		fseeko(sfp, 0, SEEK_END);
		total = ftello(sfp);
		fseeko(sfp, 0, SEEK_SET);
#endif
	}

	offset = 0;
	idx = 0;
	n = fread(buf, 1, sizeof(buf), sfp);

	unit_size = select_unit_size(buf, buf+n);
	if(unit_size < 188){
		fprintf(stderr, "error - failed on select_unit_size()\n");
		goto LAST;
	}

	do {
		curr = buf;
		tail = buf + n;
		while( (curr+unit_size) < tail ){
			if( (curr[0] != 0x47) || (curr[unit_size] != 0x47) ){
				p = resync(curr, tail, unit_size);
				if(p == NULL){
					break;
				}
				curr = p;
				if( (curr+unit_size) > tail ){
					break;
				}
			}
			extract_ts_header(&header, curr);
			if(pid[header.pid] != 0){
				m = fwrite(curr, 1, 188, dfp);
				if(m != 188){
					fprintf(stderr, "error - failed on write() [dst]\n");
					goto LAST;
				}
			}
			curr += unit_size;
		}

		offset += (curr-buf);

		if( (idx & 0x7f) == 0 ){
			if(total <= 0){
				fprintf(stderr, "\rprocessing: %5dM", (int)(offset/1024/1024));
			}else{
				n = (int)(10000*offset/total);
				fprintf(stderr, "\rprocessing: %2d.%02d%%", n/100, n%100);
			}
		}
		idx += 1;

		n = tail - curr;
		if(n > 0){
			memcpy(buf, curr, n);
		}
		m = fread(buf+n, 1, sizeof(buf)-n, sfp);
		if(m < 1){
			break;
		}
		n += m;
	}while(n > unit_size);

	curr = buf;
	tail = buf + n;
	while( (curr+188) <= tail ){
		if(curr[0] != 0x47){
			p = resync_force(curr, tail, unit_size);
			if(p == NULL){
				break;
			}
			curr = p;
			if( (p+188) > tail ){
				break;
			}
		}
		extract_ts_header(&header, curr);
		if(pid[header.pid] != 0){
			m = fwrite(curr, 1, 188, dfp);
			if(m != 188){
				fprintf(stderr, "error - failed on write() [dst]\n");
				goto LAST;
			}
		}
		curr += unit_size;
	}

	fprintf(stderr, "\rprocessing: finish\n");

LAST:
	if(dfp != NULL){
		if(dfp == stdout){
			fflush(dfp);
		}else{
			fclose(dfp);
		}
		dfp = NULL;
	}
	if(sfp != NULL){
		if(sfp != stdin){
			fclose(sfp);
		}
		sfp = NULL;
	}
}

static int select_unit_size(unsigned char *head, unsigned char *tail)
{
	int i;
	int m,n,w;
	int count[320-188];

	unsigned char *buf;

	buf = head;
	memset(count, 0, sizeof(count));

	// 1st step, count up 0x47 interval
	while( buf+188 < tail ){
		if(buf[0] != 0x47){
			buf += 1;
			continue;
		}
		m = 320;
		if( buf+m > tail){
			m = tail-buf;
		}
		for(i=188;i<m;i++){
			if(buf[i] == 0x47){
				count[i-188] += 1;
			}
		}
		buf += 1;
	}

	// 2nd step, select maximum appeared interval
	m = 0;
	n = 0;
	for(i=188;i<320;i++){
		if(m < count[i-188]){
			m = count[i-188];
			n = i;
		}
	}

	// 3rd step, verify unit_size
	w = m*n;
	if( (m < 8) || ((w+2*n) < (tail-head)) ){
		return 0;
	}

	return n;
}

static unsigned char *resync(unsigned char *head, unsigned char *tail, int unit_size)
{
	int i;
	unsigned char *buf;

	buf = head;
	tail -= unit_size * 8;
	while( buf < tail ){
		if(buf[0] == 0x47){
			for(i=1;i<8;i++){
				if(buf[unit_size*i] != 0x47){
					break;
				}
			}
			if(i == 8){
				return buf;
			}
		}
		buf += 1;
	}

	return NULL;
}

static unsigned char *resync_force(unsigned char *head, unsigned char *tail, int unit_size)
{
	int i,n;
	unsigned char *buf;

	buf = head;
	while( buf < (tail-188) ){
		if(buf[0] == 0x47){
			n = (tail - buf) / unit_size;
			if(n == 0){
				return buf;
			}
			for(i=1;i<n;i++){
				if(buf[unit_size*i] != 0x47){
					break;
				}
			}
			if(i == n){
				return buf;
			}
		}
		buf += 1;
	}

	return NULL;
}

static void extract_ts_header(TS_HEADER *dst, unsigned char *packet)
{
	dst->sync                         =  packet[0];
	dst->transport_error_indicator    = (packet[1] >> 7) & 0x01;
	dst->payload_unit_start_indicator = (packet[1] >> 6) & 0x01;
	dst->transport_priority           = (packet[1] >> 5) & 0x01;
	dst->pid = ((packet[1] & 0x1f) << 8) | packet[2];
	dst->transport_scrambling_control = (packet[3] >> 6) & 0x03;
	dst->adaptation_field_control     = (packet[3] >> 4) & 0x03;
	dst->continuity_counter           =  packet[3]       & 0x0f;
}

static void extract_adaptation_field(ADAPTATION_FIELD *dst, unsigned char *data)
{
	int n;
	unsigned char *p;
	unsigned char *tail;

	p = data;

	memset(dst, 0, sizeof(ADAPTATION_FIELD));
	if( (p[0] == 0) || (p[0] > 183) ){
		return;
	}

	dst->adaptation_field_length = p[0];
	p += 1;
	tail = p + dst->adaptation_field_length;
	if( (p+1) > tail ){
		memset(dst, 0, sizeof(ADAPTATION_FIELD));
		return;
	}

	dst->discontinuity_counter = (p[0] >> 7) & 1;
	dst->random_access_indicator = (p[0] >> 6) & 1;
	dst->elementary_stream_priority_indicator = (p[0] >> 5) & 1;
	dst->pcr_flag = (p[0] >> 4) & 1;
	dst->opcr_flag = (p[0] >> 3) & 1;
	dst->splicing_point_flag = (p[0] >> 2) & 1;
	dst->transport_private_data_flag = (p[0] >> 1) & 1;
	dst->adaptation_field_extension_flag = p[0] & 1;

	p += 1;

	if(dst->pcr_flag != 0){
		if( (p+6) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		dst->program_clock_reference = ((p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]);
		dst->program_clock_reference <<= 10;
		dst->program_clock_reference |= (((p[4]&0x80)<<2)|((p[4]&1)<<1)|p[5]);
		p += 6;
	}

	if(dst->opcr_flag != 0){
		if( (p+6) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		dst->original_program_clock_reference = ((p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]);
		dst->original_program_clock_reference <<= 10;
		dst->original_program_clock_reference |= (((p[4]&0x80)<<2)|((p[4]&1)<<1)|p[5]);
		p += 6;
	}

	if(dst->splicing_point_flag != 0){
		if( (p+1) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		dst->splice_countdown = p[0];
		p += 1;
	}

	if(dst->transport_private_data_flag != 0){
		if( (p+1) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		n = p[0];
		dst->transport_private_data_length = n;
		p += (1+n);
		if( p > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
	}

	if(dst->adaptation_field_extension_flag != 0){
		if( (p+2) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		n = p[0];
		dst->adaptation_field_extension_length = n;
		p += 1;
		if( (p+n) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		dst->ltw_flag = (p[0] >> 7) & 1;
		dst->piecewise_rate_flag = (p[0] >> 6) & 1;
		dst->seamless_splice_flag = (p[0] >> 5) & 1;
		p += 1;
		n -= 1;
		if(dst->ltw_flag != 0){
			if(n < 2){
				memset(dst, 0, sizeof(ADAPTATION_FIELD));
				return;
			}
			dst->ltw_valid_flag = (p[0] >> 7) & 1;
			dst->ltw_offset = (((p[0] & 0x7f)<<8) | p[1]);
			p += 2;
			n -= 2;
		}
		if(dst->piecewise_rate_flag != 0){
			if(n < 3){
				memset(dst, 0, sizeof(ADAPTATION_FIELD));
				return;
			}
			dst->piecewise_rate = (((p[0] & 0x3f)<<16)|(p[1]<<8)|p[2]);
			p += 3;
			n -= 3;
		}
		if(dst->seamless_splice_flag != 0){
			if(n < 5){
				memset(dst, 0, sizeof(ADAPTATION_FIELD));
				return;
			}
			dst->splice_type = (p[0] >> 4) & 0x0f;
			dst->dts_next_au = (((p[0]&0x0e)<<14)|(p[1]<<7)|((p[2]>>1)&0x7f));
			dst->dts_next_au <<= 15;
			dst->dts_next_au |= ((p[3]<<7)|((p[4]>>1)&0x7f));
			p += 5;
			n -= 5;
		}
		p += n;
	}

}

static int check_unit_invert(unsigned char *head, unsigned char *tail)
{
	unsigned char *buf;

	buf = tail-188;

	while(head <= buf){
		if(buf[0] == 0x47){
			return tail-buf;
		}
		buf -= 1;
	}

	return 0;
}

static void add_drop_info(RESYNC_REPORT *report, int count, int pid, int64_t pos)
{
	int idx;
	int n;

	idx = count - 1;
	if( (report == NULL) || (idx < 0) ){
		// do nothing
		return;
	}

	if(report[idx].drop_count < 4){
		n = (int) report[idx].drop_count;
		report[idx].drop_pid[n] = (short)pid;
		report[idx].drop_pos[n] = pos;
	}

	report[idx].drop_count += 1;
}

static void print_resync_report(RESYNC_REPORT *report, int count)
{
	int i,j;
	int m,n;
	int year,month,date;

	printf("total sync error: %d\n", count);

	m = count;
	if(report == NULL){
		m = 0;
	}

	for(i=0;i<m;i++){
		printf("  resync[%d]%s : miss=%"PRId64", sync=%"PRId64", drop=%"PRId64"\n",
		       i, (report[i].miss == report[i].sync ? "(drop only)" : ""),
		       report[i].miss, report[i].sync, report[i].drop_count);
		if(report[i].last_mjd || report[i].mjd){
			if(report[i].last_mjd){
				mjd_to_ymd(report[i].last_mjd, &year, &month, &date);
				printf("    time : %d-%02d-%02dT%02d:%02d:%02d",
				       year, month, date, report[i].last_h, report[i].last_m, report[i].last_s);
			}else{
				printf("    time : --");
			}
			if(report[i].mjd){
				mjd_to_ymd(report[i].mjd, &year, &month, &date);
				printf(", %d-%02d-%02dT%02d:%02d:%02d\n",
				       year, month, date, report[i].h, report[i].m, report[i].s);
			}else{
				printf(", --\n");
			}
		}
		n = (int)report[i].drop_count;
		if(n > 4){
			n = 4;
		}
		for(j=0;j<n;j++){
			printf("    drop[%d] : pid=0x%04x, pos=%"PRId64"\n", j, report[i].drop_pid[j], report[i].drop_pos[j]);
		}
	}
}

static void set_resync_report_last_time(RESYNC_REPORT *report, int mjd, int h, int m, int s)
{
	report->last_mjd = mjd;
	report->last_h = h;
	report->last_m = m;
	report->last_s = s;
}

static void update_resync_report_time(RESYNC_REPORT *report, int count, int mjd, int h, int m, int s)
{
	int i;

	for(i = count - 1; report && i >= 0 && !report[i].mjd; i--){
		report[i].mjd = mjd;
		report[i].h = h;
		report[i].m = m;
		report[i].s = s;
	}
}

static void mjd_to_ymd(int mjd, int *y, int *m, int *d)
{
	int yd = (mjd * 20 - 301564) / 7305;
	int md = (mjd * 10000 - 149561000 - yd * 1461 / 4 * 10000) / 306001;
	int k = (md == 14 || md == 15) ? 1 : 0;

	*y = yd + k + 1900;
	*m = md - 1 - k * 12;
	*d = mjd - 14956 - yd * 1461 / 4 - md * 306001 / 10000;
}

static int find_packet_time_data(unsigned char **time_data, const TS_HEADER *hdr, unsigned char *packet)
{
	unsigned char *p;

	int table_id;
	int length;

	p = packet + 4;
	if(hdr->adaptation_field_control & 0x02){
		if(p[0] > 182){
			return 0;
		}
		/* adaptation length */
		p += (p[0]+1);
	}
	if(hdr->payload_unit_start_indicator == 0){
		/* do nothing */
		return 0;
	}
	if((int)(p - packet) + p[0] > 184){
		return 0;
	}
	/* pointer */
	p += (p[0]+1);

	table_id = p[0];
	length = ((p[1]<<8)|p[2]) & 0x0fff;
	p += 3;
	if(length < 5 || (int)(p - packet) + 5 > 188){
		return 0;
	}

	*time_data = p;
	return table_id;
}

static void show_tdt_or_tot(TS_HEADER *hdr, unsigned char *packet, int64_t pos)
{
	unsigned char *p;
	int table_id = find_packet_time_data(&p, hdr, packet);

	if(table_id == 0x70){
		/* TDT */
		fprintf(stdout, "TDT: [%02x:%02x:%02x] offset=%"PRId64"\n", p[2], p[3], p[4], pos);
	}else if(table_id == 0x73){
		/* TOT */
		fprintf(stdout, "TOT: [%02x:%02x:%02x] offset=%"PRId64"\n", p[2], p[3], p[4], pos);
	}
}

