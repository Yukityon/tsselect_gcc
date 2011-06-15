・名称 

　tsselect - MPEG-2 TS stream(pid) selector

・機能

　MPEG-2 TS を読み込んで
　・PID 別に、パケット数・同期エラー数・ドロップ数・スクランブル
　　パケット数を表示する
　・指定された PID だけを含むストリームに再構築する
　・指定された PID 以外を含むストリームに再構築する

・バージョン

　0.1.8

・一次配布元

　http://www.marumo.ne.jp/junk/tsselect.lzh
　又は
　http://www.marumo.ne.jp/junk/tsselect-0.1.8.lzh

　バージョン番号が付与されていない側は、バージョンアップ時に
　上書きされていきます

・二次配布等に関して

　オリジナルに改変を加えない場合であれば、その目的・手段を問わ
　ず複製・公衆再送信を許可します

・ソースコードの利用に関して

　・ソースコードの利用によってプログラムにバグが混入しても茂木
　　和洋は責任を負わない
　・ソースコードの利用によって特許関連のトラブルが発生しても
　　茂木 和洋は責任を負わない

　上記２条件に同意して作成された二次的著作物に対して、原著作者
　に与えられる諸権利は行使しません

・使い方 [TS 情報表示]

　tsselect nd14.ts > nd14.log

　nd14.ts を読み込んで、nd14.log に次のようなログを保存します

　　total sync error: 3
　　  resync[0] : miss=0x000000000000, sync=0x000000000059, drop=0
　　  resync[1] : miss=0x000053be0745, sync=0x000053be0827, drop=11
　　    drop[0] : pid=0x1008, pos=0x000053be0827
　　    drop[1] : pid=0x1006, pos=0x000053be0a5b
　　    drop[2] : pid=0x1007, pos=0x000053be0bd3
　　    drop[3] : pid=0x1002, pos=0x000053be0d4b
　　  resync[2] : miss=0x000053c10793, sync=0x000053c10833, drop=22
　　    drop[0] : pid=0x1008, pos=0x000053c10833
　　    drop[1] : pid=0x1006, pos=0x000053c108ef
　　    drop[2] : pid=0x1007, pos=0x000053c10b23
　　    drop[3] : pid=0x1002, pos=0x000053c10d57
　　pid=0x0000, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0001, total=      29, d=  0, e=  0, scrambling=0
　　pid=0x0010, total=     943, d=  0, e=  0, scrambling=0
　　pid=0x0011, total=    1235, d=  0, e=  0, scrambling=0
　　pid=0x0012, total=  128217, d=  2, e=  0, scrambling=0
　　pid=0x0014, total=      59, d=  0, e=  0, scrambling=0
　　pid=0x0023, total=      81, d=  0, e=  0, scrambling=0
　　pid=0x0024, total=      90, d=  0, e=  0, scrambling=0
　　pid=0x0025, total=     253, d=  0, e=  0, scrambling=0
　　pid=0x0101, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0102, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0106, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0107, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0108, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0501, total=    7359, d=  0, e=  0, scrambling=0
　　pid=0x0502, total=    7357, d=  0, e=  0, scrambling=0
　　pid=0x0506, total=    9811, d=  0, e=  0, scrambling=0
　　pid=0x0507, total=    9812, d=  0, e=  0, scrambling=0
　　pid=0x0508, total=    4906, d=  0, e=  0, scrambling=0
　　pid=0x0701, total=   33331, d=  0, e=  0, scrambling=0
　　pid=0x0811, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0812, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0816, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0817, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0818, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x1001, total=  883505, d=  2, e=  0, scrambling=883505
　　pid=0x1002, total=  883506, d=  2, e=  0, scrambling=883506
　　pid=0x1006, total= 1084884, d=  2, e=  0, scrambling=1084884
　　pid=0x1007, total= 1084836, d=  2, e=  0, scrambling=1084836
　　pid=0x1008, total= 2751717, d=  2, e=  0, scrambling=2751717
　　pid=0x1101, total=   26091, d=  2, e=  0, scrambling=26091
　　pid=0x1102, total=   26090, d=  2, e=  0, scrambling=26090
　　pid=0x1106, total=   39137, d=  2, e=  0, scrambling=39137
　　pid=0x1107, total=   39135, d=  2, e=  0, scrambling=39135
　　pid=0x1108, total=   31733, d=  2, e=  0, scrambling=31733
　　pid=0x1fff, total= 3123528, d=  0, e=  0, scrambling=0

　これは、同期エラー表示部と各ストリーム (pid) 毎の情報表示部に
　分かれていて

　前半の

　　total sync error: 3
　　  resync[0] : miss=0x000000000000, sync=0x000000000059, drop=0
　　  resync[1] : miss=0x000053be0745, sync=0x000053be0827, drop=11
　　    drop[0] : pid=0x1008, pos=0x000053be0827
　　    drop[1] : pid=0x1006, pos=0x000053be0a5b
　　    drop[2] : pid=0x1007, pos=0x000053be0bd3
　　    drop[3] : pid=0x1002, pos=0x000053be0d4b
　　  resync[2] : miss=0x000053c10793, sync=0x000053c10833, drop=22
　　    drop[0] : pid=0x1008, pos=0x000053c10833
　　    drop[1] : pid=0x1006, pos=0x000053c108ef
　　    drop[2] : pid=0x1007, pos=0x000053c10b23
　　    drop[3] : pid=0x1002, pos=0x000053c10d57

　という部分が同期エラー表示部になります
　ここでは同期エラーの数と各同期エラーの詳細を表示しています

　先頭の

　　total sync error: 3

　という行は検出された同期エラーの数を表示しています
　このファイルでは 3 個の同期エラーが検出されていて、次の行から
　各同期エラーの詳細が全て表示されていますが、9 個以上の同期
　エラーが発生している場合、先頭 8 個までの同期エラーしか詳細
　表示は行われません

　次の 

　　  resync[0] : miss=0x000000000000, sync=0x000000000059, drop=0

　という行は、取り込み開始が TS パケットの先頭と一致してなく、
　0x59 (89) バイトから TS パケットが始まっていることを示しています
　これ自体は取りこぼしが発生していることを意味しているわけでは
　ありません

　その次の

　　  resync[1] : miss=0x000053be0745, sync=0x000053be0827, drop=11

　という行は、ファイルの途中で TS パケットの同期コード (0x47) が
　正しい位置に存在しなくなり、少し先で同期コードと思われる部分を
　発見できたけれども、その後に 11 個のドロップエラーを検出したと
　いうことを示しています
　これは、PC 側の処理が間に合わなかった等の問題で、データを取り
　こぼしてしまい、途中のデータが欠落している可能性の高い例です

　これに続く

　　    drop[0] : pid=0x1008, pos=0x000053be0827
　　    drop[1] : pid=0x1006, pos=0x000053be0a5b
　　    drop[2] : pid=0x1007, pos=0x000053be0bd3
　　    drop[3] : pid=0x1002, pos=0x000053be0d4b

　という 4 行は、同期エラー後に検出したドロップエラーのうち、
　先頭から 4 個の詳細を表示している部分です
　ここではドロップエラーを検出した pid とファイル先頭からの位置
　を表示しています

　後半の

　　pid=0x0000, total=    2943, d=  1, e=  0, scrambling=0
　　pid=0x0001, total=      29, d=  0, e=  0, scrambling=0
　　pid=0x0010, total=     943, d=  0, e=  0, scrambling=0
　　pid=0x0011, total=    1235, d=  0, e=  0, scrambling=0
　　pid=0x0012, total=  128217, d=  2, e=  0, scrambling=0
　　pid=0x0014, total=      59, d=  0, e=  0, scrambling=0
　　……

　という部分は各ストリームの情報を表示する部分です
　total=XXXX の部分がその pid の総パケット数を
　d=XX の部分がカウンターエラーを検出したパケット数を
　e=XX の部分はビットエラーのあった (チューナ内でのエラー訂正に
　失敗した) パケット数を
　scrambling=XXXX の部分が暗号化されているパケット数を
　それぞれ示しています

・使い方 [TS 再構成]

　tsselect src.m2t dst.m2t 0x0111 0x112

　src.m2t を読み込んで PID が 0x0111 と 0x112 のパケットだけを
　dst.m2t に出力します

・使い方 [TS 再構成/指定PID除外]

　tsselect src.m2t dst.m2t -x 0x0012 0x0014

　src.m2t を読み込んで PID が 0x0012 と 0x0014 のパケット以外を
　全て dst.m2t に出力します

・注意事項

　204 byte (リードソロモン付き放送波) TS や　192 byte (サイクル
　カウント・サイクルオフセット付き IEEE 1394) TS に対して TS 
　再構成を行うと、188 byte の標準 TS に変換して出力します

　当初はオリジナルサイズのまま再構成しようかと思っていたのですが、
　204 byte TS ではパケットの後ろの 16 byte がペアになるのに対して
　192 byte TS ではパケットの前の 4 byte がペアになるといった形で
　汎用的に作るのが面倒だったのでこーゆー作りになってます

更新履歴

・2009, 9/3 ver. 0.1.8
　TS 再構成の際に指定 pid 以外を保存する -x オプションを追加

・2009, 5/21 ver. 0.1.7

　TS 再構成時に、pid として範囲外の値を設定した場合に例外を発生
　させるバグを修正

　readme.txt (このファイル) に二次配布・ソースコードの利用に関し
　ての条件を追記

・2008, 5/2 ver. 0.1.6

　TS 情報表示の際、ビットエラー発生 (チューナモジュール内でのエラー
　訂正に失敗した) パケット数を表示する機能を追加

　ユニットサイズの判定方法を変更

・2008, 3/28 ver. 0.1.5

　進捗を nn.nn% の書式で標準エラー出力に表示するように変更

　同期エラー (ユニットサイズ単位での 0x47 検出に失敗する場合) も
　TS 情報表示の際にログ出力対象とする形に変更
　トータル同期エラー数と、先頭 8 個までのエラー詳細を表示する

　ドロップ検出の際に adaptation field の discontinuity_counter 
　を考慮するように変更

・2007, 11/18 ver. 0.1.4

　192 byte TS を入力に与えた場合、終端処理で無限ループに落ちていた
　バグを修正

・2007, 11/17 ver. 0.1.3

　null packet (pid=0x1fff) をドロップ集計の対象外に変更

・2007, 11/16 ver. 0.1.2

　エラーメッセージの typo を修正

・2007, 11/16 ver. 0.1.1

　drop カウントを真面目に行うように変更

・2007, 11/10 ver. 0.1.0

　公開
