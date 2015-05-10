#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define __int64 int64_t


typedef struct {

	int           pid;
	int           last_continuity_counter;

	__int64       first;
	__int64       total;
	__int64       error;
	__int64       drop;
	__int64       scrambling;

	unsigned char last_packet[188];
	int           duplicate_count;

} TS_STATUS;

typedef struct {

	__int64       miss;
	__int64       sync;

	__int64       drop_count;

	short         drop_pid[4];
	__int64       drop_pos[4];

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

	__int64       program_clock_reference;
	__int64       original_program_clock_reference;

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
	__int64       dts_next_au;

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

static void add_drop_info(RESYNC_REPORT *report, int count, int max, int pid, __int64 pos);
static void print_resync_report(RESYNC_REPORT *report, int count, int max);

static void show_tdt_or_tot(TS_HEADER *hdr, unsigned char *packet, __int64 pos);

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
	fprintf(stderr, "usage: tsselect src.m2t [dst.m2t pid  [more pid ..]]\n");
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
	int fd;

	int pid;
	int idx;
	int lcc;
	int i,m,n;
	int unit_size;

	__int64 offset;
	__int64 total;

	TS_STATUS *stat;
	TS_HEADER  header;
	ADAPTATION_FIELD adapt;

	RESYNC_REPORT resync_report[8];
	int resync_count;
	int resync_log_max;

	unsigned char *p;
	unsigned char *curr;
	unsigned char *tail;

	unsigned char buf[8192];

	fd = -1;
	stat = NULL;

	memset(resync_report, 0, sizeof(resync_report));
	resync_log_max = sizeof(resync_report)/sizeof(RESYNC_REPORT);
	resync_count = 0;

	fd = open(path, O_RDONLY);
	if(fd < 0){
		fprintf(stderr, "error - failed on open(%s) [src]\n", path);
		goto LAST;
	}

	lseek64(fd, 0, SEEK_END);
	total = lseek64(fd, 0, SEEK_CUR);
	lseek64(fd, 0, SEEK_SET);

	stat = (TS_STATUS *)calloc(8192, sizeof(TS_STATUS));
	if(stat == NULL){
		fprintf(stderr, "error - failed on malloc(size=%ld)\n", sizeof(TS_STATUS)*8192);
		goto LAST;
	}

	for(i=0;i<8192;i++){
		stat[i].pid = i;
		stat[i].last_continuity_counter = -1;
		stat[i].first = -1;
	}

	offset = 0;
	idx = 0;
	n = read(fd, buf, sizeof(buf));

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
				if(resync_count < resync_log_max){
					resync_report[resync_count].miss = offset+(curr-buf);
				}
				p = resync(curr, tail, unit_size);
				if(p == NULL){
					break;
				}
				curr = p;
				if(resync_count < resync_log_max){
					resync_report[resync_count].sync = offset+(curr-buf);
				}
				resync_count += 1;
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
			if(stat[pid].first < 0){
				stat[pid].first = offset + (curr-buf);
			}
			lcc = stat[pid].last_continuity_counter;
			if( (lcc >= 0) && (adapt.discontinuity_counter == 0) ){
				if( pid == 0x1fff ){
					// null packet - drop count has no mean
					// do nothing
				}else if( (header.adaptation_field_control & 0x01) == 0 ){
					// no payload : continuity_counter should not increment
					if(lcc != header.continuity_counter){
						stat[pid].drop += 1;
						add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
					}
				}else if(lcc == header.continuity_counter){
					// has payload and same continuity_counter
					if(memcmp(stat[pid].last_packet, curr, 188) != 0){
						// non-duplicate packet
						stat[pid].drop += 1;
						add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
					}
					stat[pid].duplicate_count += 1;
					if(stat[pid].duplicate_count > 1){
						// duplicate packet count exceeds limit (two)
						stat[pid].drop += 1;
						add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
					}
				}else{
					m = (lcc + 1) & 0x0f;
					if(m != header.continuity_counter){
						stat[pid].drop += 1;
						add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
					}
					stat[pid].duplicate_count = 0;
				}
			}
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

		if( (idx & 0x0f) == 0 ){
			n = (int)(10000*offset/total);
			fprintf(stderr, "\rprocessing: %2d.%02d%%", n/100, n%100);
		}
		idx += 1;

		n = tail - curr;
		if(n > 0){
			memcpy(buf, curr, n);
		}
		m = read(fd, buf+n, sizeof(buf)-n);
		if(m < 1){
			break;
		}
		n += m;

	} while(n > unit_size);

	curr = buf;
	tail = buf + n;
	while( (curr+188) <= tail ){
		if(curr[0] != 0x47){
			if(resync_count < resync_log_max){
				resync_report[resync_count].miss = offset+(curr-buf);
			}
			p = resync_force(curr, tail, unit_size);
			if(p == NULL){
				break;
			}
			curr = p;
			if(resync_count < resync_log_max){
				resync_report[resync_count].sync = offset+(curr-buf);
			}
			resync_count += 1;
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
			if( pid == 0x1fff ){
				// null packet - drop count has no mean
				// do nothing
			}else if( (header.adaptation_field_control & 0x01) == 0 ){
				// no payload : continuity_counter should not increment
				if(lcc != header.continuity_counter){
					stat[pid].drop += 1;
					add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
				}
			}else if(lcc == header.continuity_counter){
				// has payload and same continuity_counter
				if(memcmp(stat[pid].last_packet, curr, 188) != 0){
					// non-duplicate packet
					stat[pid].drop += 1;
					add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
				}
				stat[pid].duplicate_count += 1;
				if(stat[pid].duplicate_count > 1){
					// duplicate packet count exceeds limit (two)
					stat[pid].drop += 1;
					add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
				}
			}else{
				m = (lcc + 1) & 0x0f;
				if(m != header.continuity_counter){
					stat[pid].drop += 1;
					add_drop_info(resync_report, resync_count, resync_log_max, pid, offset+(curr-buf));
				}
				stat[pid].duplicate_count = 0;
			}
		}
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

LAST:
	if(resync_count > 0){
		print_resync_report(resync_report, resync_count, resync_log_max);
	}

	if(stat){
		for(i=0;i<8192;i++){
			if(stat[i].total > 0){
				printf("pid=0x%04x, total=%8ld, d=%3ld, e=%3ld, scrambling=%ld, offset=%ld\n", i, stat[i].total, stat[i].drop, stat[i].error, stat[i].scrambling, stat[i].first);
			}
		}
		free(stat);
		stat = NULL;
	}

	if(fd >= 0){
		close(fd);
		fd = -1;
	}
}

static void tsselect(const char *src, const char *dst, const unsigned char *pid)
{
	int sfd,dfd;

	int m,n;
	int idx;
	int unit_size;

	TS_HEADER header;

	__int64 offset;
	__int64 total;

	unsigned char *p;
	unsigned char *curr;
	unsigned char *tail;

	unsigned char buf[8192];

	sfd = -1;
	dfd = -1;

	sfd = open(src, O_RDONLY);
	if(sfd < 0){
		fprintf(stderr, "error - failed on open(%s) [src]\n", src);
		goto LAST;
	}

	dfd = open(dst, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IROTH);
	if(dfd < 0){
		fprintf(stderr, "error - failed on open(%s) [dst]\n", dst);
		goto LAST;
	}

	lseek64(sfd, 0, SEEK_END);
	total = lseek(sfd,0,SEEK_CUR);
	lseek64(sfd, 0, SEEK_SET);

	offset = 0;
	idx = 0;
	n = read(sfd, buf, sizeof(buf));

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
				m = write(dfd, curr, 188);
				if(m != 188){
					fprintf(stderr, "error - failed on write() [dst]\n");
					goto LAST;
				}
			}
			curr += unit_size;
		}

		offset += (curr-buf);

		if( (idx & 0x0f) == 0 ){
			n = (int)(10000*offset/total);
			fprintf(stderr, "\rprocessing: %2d.%02d%%", n/100, n%100);
		}
		idx += 1;

		n = tail - curr;
		if(n > 0){
			memcpy(buf, curr, n);
		}
		m = read(sfd, buf+n, sizeof(buf)-n);
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
			m = write(dfd, curr, 188);
			if(m != 188){
				fprintf(stderr, "error - failed on write() [dst]\n");
				goto LAST;
			}
		}
		curr += unit_size;
	}

	fprintf(stderr, "\rprocessing: finish\n");

LAST:
	if(dfd >= 0){
		close(dfd);
		dfd = -1;
	}
	if(sfd >= 0){
		close(sfd);
		sfd = -1;
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

static void add_drop_info(RESYNC_REPORT *report, int count, int max, int pid, __int64 pos)
{
	int idx;
	int n;

	idx = count - 1;
	if( (idx >= max) || (idx < 0) ){
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

static void print_resync_report(RESYNC_REPORT *report, int count, int max)
{
	int i,j;
	int m,n;

	printf("total sync error: %d\n", count);

	m = count;
	if(m > max){
		m = max;
	}

	for(i=0;i<m;i++){
		printf("  resync[%d] : miss=0x%012lx, sync=0x%012lx, drop=%ld\n", i, report[i].miss, report[i].sync, report[i].drop_count);
		n = (int)report[i].drop_count;
		if(n > 4){
			n = 4;
		}
		for(j=0;j<n;j++){
			printf("    drop[%d] : pid=0x%04x, pos=0x%012lx\n", j, report[i].drop_pid[j], report[i].drop_pos[j]);
		}
	}
}

static void show_tdt_or_tot(TS_HEADER *hdr, unsigned char *packet, __int64 pos)
{
	unsigned char *p;

	int table_id;
	int length;

	p = packet + 4;
	if(hdr->adaptation_field_control & 0x02){
		p += (p[0]+1);
	}
	if(hdr->payload_unit_start_indicator == 0){
		/* do nothing */
		return;
	}
	p += (p[0]+1);

	table_id = p[0];
	length = ((p[1]<<8)|p[2]) & 0x0fff;
	p += 3;

	if(table_id == 0x70){
		/* TDT */
		fprintf(stdout, "TDT: [%02x:%02x:%02x] offset=%ld\n", p[2], p[3], p[4], pos);
	}else if(table_id == 0x73){
		/* TOT */
		fprintf(stdout, "TOT: [%02x:%02x:%02x] offset=%ld\n", p[2], p[3], p[4], pos);
	}
}

