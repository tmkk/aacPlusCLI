#pragma warning(disable:4996)
/*
 * BIG FAT WARNING:
 * this code based on "The Winamp Transcoder" plugin (http://www.srcf.ucam.org/~wdhf2/transcoder/)
 *
 * Copyright (c) 2005 Dmirty Alexandrov aka dimzon aka dimzon541 (dimzon541@gmail.com)
 * Copyright (c) 2004 Will Fisher (will.fisher@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*


********************************************************************
* AACPlus v2 Encoder (using WinAmp 5.1 enc_aacplus.dll)
* BIG FAT WARNING:
*	this code based on "The Winamp Transcoder" plugin
*	http://www.srcf.ucam.org/~wdhf2/transcoder/
* Source timestamp Fri Nov 18 13:22:23 2005
* Build Nov 18 2005, 13:25:23
********************************************************************
* NOTE!	enc_aacplus.dll must be into executable directory
*	get it from Winamp 5.1 plugins directory
********************************************************************
Usage:
	aacEncPlus.EXE <wav_file> <bitstream_file> [options]
Options:
	--cbr <bitrate>	- Set bitrate (CBR) to <bitrate> bps. Default is 64000
	--chmode <mode>	- integer between 1-5, default is 2
		1 - Mono
		2 - Stereo
		3 - Stereo Independent
		4 - Parametric (you MUST set use V2 encoder and <bitrate> can't exeed 48000)
		5 - Dual Channel
	--no-v2	- Use AACPlus v1 encoder instead v2
	--speech - Tune For Speech
	--pns - Enable Perceptual Noise Subsitution
	--no-progress	- Disable progress display
	--mpeg4aac  - Produce MPEG4 AAC isntead of MPEG2 AAC (experimental!)
	--mp4  - Wrap result into MPEG4 container (libmp4v2.dll must be into executable directory)
	--rawpcm <rate> <cnt> <bp>	- Signal RAW PCM input intead of WAV
		<rate>	- Samplerate in Hz (32000, 44100 or 48000)
		<cnt>	- Channels count (1 or 2)
		<bp>	- Bit's per sample (8 or 16)
Example:
	aacEncPlus.EXE input.wav out.aac --cbr 56000
	aacEncPlus.EXE input.wav out.aac --cbr 48000 --chmode 4 --mpeg4aac
	aacEncPlus.EXE input.raw out.aac --mpeg4aac  --rawpcm 44100 2 16
	aacEncPlus.EXE input.wav out.m4a --cbr 32000 --chmode 4 --mpeg4aac --mp4

WARNING: this encoder can read and encode data from stdin:
	use - as input filename
Example:
	aacEncPlus.EXE - out.aac --cbr 56000
	aacEncPlus.EXE - out.aac --cbr 48000 --chmode 4 --mpeg4aac
	aacEncPlus.EXE - out.aac --mpeg4aac  --rawpcm 44100 2 16
	aacEncPlus.EXE - out.m4a --cbr 32000 --chmode 4 --mpeg4aac --mp4


*/

#include "stdafx.h"
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <process.h>

// transcode() returns one of these:
#define TRANSCODE_FINISHED_OK        0
#define ERROR_CANNOT_LOAD_DECODER   -1
#define ERROR_CANNOT_OPEN_DECODER   -2
#define ERROR_CANNOT_LOAD_ENCODER   -3
#define ERROR_CANNOT_OPEN_ENCODER   -4
#define ERROR_INCOMPATABLE_DECODER  -5
#define ERROR_CANNOT_OPEN_OUTFILE   -6
#define ERROR_CANNOT_OPEN_INFILE    -7
#define ERROR_ABORTED               -8
// the main function may also return
#define ERROR_INSUFFICIENT_ARGS     -9

#define BUFSIZE 32768

#define SWAP32(n) ((((n)>>24)&0xff) | (((n)>>8)&0xff00) | (((n)<<8)&0xff0000) | (((n)<<24)&0xff000000))
#define SWAP16(n) ((((n)>>8)&0xff) | (((n)<<8)&0xff00))

#ifdef _MSC_VER
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

class AudioCoder
{
  public:
    AudioCoder() { }
    virtual int Encode(int framepos, void *in, int in_avail, int *in_used, void *out, int out_avail)=0; //returns bytes in out
    virtual ~AudioCoder() { };
};

typedef struct wavHeader {
  unsigned int chunkid;
  int totdatasize;
  unsigned int rifftype;
  unsigned int fmt;
  int fmtsize;
  unsigned short ccode;
  short nch;
  int srate;
  int bytespersec;
  short blockalign;
  short bps;
  unsigned int data;
  unsigned int datasize;
} wavHeader;

static void optimizeAtoms(FILE *fp, __int64 origSize)
{
	unsigned int tmp;
	unsigned int moovSize;
	char atom[4];
	
	if(!fp) return;
	
	unsigned int bufferSize = 1024*1024;
	char *tmpbuf = (char *)malloc(bufferSize);
	char *tmpbuf2 = (char *)malloc(bufferSize);
	char *read = tmpbuf;
	char *write = tmpbuf2;
	char *swap;
	char *moovbuf = NULL;
	int i;
	bool moov_after_mdat = false;
	
	while(1) { //skip until moov;
		if(fread(&tmp,4,1,fp) < 1) goto end;
		if(fread(atom,1,4,fp) < 4) goto end;
		tmp = SWAP32(tmp);
		if(!memcmp(atom,"moov",4)) break;
		if(!memcmp(atom,"mdat",4)) moov_after_mdat = true;
		if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
	}
	
	if(!moov_after_mdat) goto end;
	
	__int64 pos_moov = ftello(fp) - 8;
	moovSize = tmp;

	unsigned int bytesRead = 0;
	while(bytesRead < moovSize-8) {
		if(fread(&tmp,4,1,fp) < 1) goto end;
		if(fread(atom,1,4,fp) < 4) goto end;
		tmp = SWAP32(tmp);
		bytesRead += tmp;
		if(memcmp(atom,"trak",4)) {
			if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
			continue;
		}

		__int64 pos_next = ftello(fp) + tmp - 8;
		
		while(1) { //skip until mdia;
			if(fread(&tmp,4,1,fp) < 1) goto end;
			if(fread(atom,1,4,fp) < 4) goto end;
			tmp = SWAP32(tmp);
			if(!memcmp(atom,"mdia",4)) break;
			if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
		}
		
		while(1) { //skip until minf;
			if(fread(&tmp,4,1,fp) < 1) goto end;
			if(fread(atom,1,4,fp) < 4) goto end;
			tmp = SWAP32(tmp);
			if(!memcmp(atom,"minf",4)) break;
			if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
		}
		
		while(1) { //skip until stbl;
			if(fread(&tmp,4,1,fp) < 1) goto end;
			if(fread(atom,1,4,fp) < 4) goto end;
			tmp = SWAP32(tmp);
			if(!memcmp(atom,"stbl",4)) break;
			if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
		}
		
		while(1) { //skip until stco;
			if(fread(&tmp,4,1,fp) < 1) goto end;
			if(fread(atom,1,4,fp) < 4) goto end;
			tmp = SWAP32(tmp);
			if(!memcmp(atom,"stco",4)) break;
			if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
		}
		
		int *stco = (int *)malloc(tmp-8);
		if(fread(stco,1,tmp-8,fp) < tmp-8) goto end;
		int nElement = SWAP32(stco[1]);
		
		/* update stco atom */
		for(i=0;i<nElement;i++) {
			stco[2+i] = SWAP32(SWAP32(stco[2+i])+moovSize);
		}
		if(fseeko(fp,8-(int)tmp,SEEK_CUR) != 0) goto end;
		if(fwrite(stco,1,tmp-8,fp) < tmp-8) goto end;
		
		free(stco);

		if(fseeko(fp,pos_next,SEEK_SET) != 0) goto end;
	}
	
	rewind(fp);
	
	/* save moov atom */
	
	moovbuf = (char *)malloc(moovSize);
	if(fseeko(fp,pos_moov,SEEK_SET) != 0) goto end;
	if(fread(moovbuf,1,moovSize,fp) < moovSize) goto end;
	rewind(fp);
	
	while(1) { //skip until ftyp;
		if(fread(&tmp,4,1,fp) < 1) goto end;
		if(fread(atom,1,4,fp) < 4) goto end;
		tmp = SWAP32(tmp);
		if(!memcmp(atom,"ftyp",4)) break;
		if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
	}
	
	/* position after ftyp atom is the inserting point */
	if(fseeko(fp,tmp-8,SEEK_CUR) != 0) goto end;
	pos_moov = ftello(fp);
	
	/* optimize */
	
	__int64 bytesToMove = origSize-pos_moov-moovSize;
	
	if(bytesToMove < moovSize) {
		if(bufferSize < bytesToMove) {
			tmpbuf = (char *)realloc(tmpbuf,(size_t)bytesToMove);
			read = tmpbuf;
		}
		if(fread(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
		if(fseeko(fp,(__int64)moovSize-bytesToMove,SEEK_CUR) != 0) goto end;
		if(fwrite(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
	}
	else if(bytesToMove > bufferSize) {
		if(bufferSize < moovSize) {
			tmpbuf = (char *)realloc(tmpbuf,moovSize);
			tmpbuf2 = (char *)realloc(tmpbuf2,moovSize);
			read = tmpbuf;
			write = tmpbuf2;
			bufferSize = moovSize;
			if(bytesToMove <= bufferSize) goto moveBlock_is_smaller_than_buffer;
		}
		if(fread(write,1,bufferSize,fp) < bufferSize) goto end;
		bytesToMove -= bufferSize;
		while(bytesToMove > bufferSize) {
			if(fread(read,1,bufferSize,fp) < bufferSize) goto end;
			if(fseeko(fp,(__int64)moovSize-2*(__int64)bufferSize,SEEK_CUR) != 0) goto end;
			if(fwrite(write,1,bufferSize,fp) < bufferSize) goto end;
			if(fseeko(fp,(__int64)bufferSize-(__int64)moovSize,SEEK_CUR) != 0) goto end;
			swap = read;
			read = write;
			write = swap;
			bytesToMove -= bufferSize;
			//NSLog(@"DEBUG: %d bytes left",bytesToMove);
		}
		if(fread(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
		if(fseeko(fp,(__int64)moovSize-(__int64)bufferSize-bytesToMove,SEEK_CUR) != 0) goto end;
		if(fwrite(write,1,bufferSize,fp) < bufferSize) goto end;
		if(fwrite(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
	}
	else {
moveBlock_is_smaller_than_buffer:
		if(fread(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
		if(moovSize < bytesToMove) {
			if(fseeko(fp,(__int64)moovSize-bytesToMove,SEEK_CUR) != 0) goto end;
		}
		else {
			if(fseeko(fp,0-bytesToMove,SEEK_CUR) != 0) goto end;
			if(fwrite(moovbuf,1,moovSize,fp) < moovSize) goto end;
		}
		if(fwrite(read,1,(size_t)bytesToMove,fp) < bytesToMove) goto end;
	}
	
	if(fseeko(fp,pos_moov,SEEK_SET) != 0) goto end;
	if(fwrite(moovbuf,1,moovSize,fp) < moovSize) goto end;
	
end:
	if(moovbuf) free(moovbuf);
	free(tmpbuf);
	free(tmpbuf2);
}

int _encode(FILE * hFile, AudioCoder * encoder,  int len, char * buf, int bMPEG4AAC)
{
	static char outbuffer[BUFSIZE];
	DWORD dwBytesWritten=0;
	int i=0;
	for(;;)
	{
		int in_used=0;
		//if(!len) break; - we must finalize it !
		int enclen=encoder->Encode(i++,buf,len,&in_used,outbuffer,sizeof(outbuffer));

		if(enclen>0)
		{
			if(0==bMPEG4AAC) outbuffer[1] |= 0x08;  // force MPEG2
			if(1==bMPEG4AAC) outbuffer[1] &= 0xf7;  // force MPEG4
			fwrite(outbuffer, 1, enclen, hFile);
		}

		if(in_used>0)
		{
			buf += in_used;
			len -= in_used;
		}

		if(((in_used <=0) && (enclen <=0)) || (0==len)) // Detect unbreakable loop
			break;
	}
	return 0;
}

int _finalize(FILE * hFile, AudioCoder * encoder, char * buf, int bMPEG4AAC)
{
	static char outbuffer[BUFSIZE];
	DWORD dwBytesWritten=0;
	int i=0;
	for(;;)
	{
		int in_used=0;
		//if(!len) break; - we must finalize it !
		int enclen=encoder->Encode(i++,buf,0,&in_used,outbuffer,sizeof(outbuffer));
		if(enclen>0)
		{
			if(0==bMPEG4AAC) outbuffer[1] |= 0x08;  // force MPEG2
			if(1==bMPEG4AAC) outbuffer[1] &= 0xf7;  // force MPEG4
			fwrite(outbuffer, 1, enclen, hFile);
		}
		else break;
	}
	return 0;
}

void showLogo()
{
	printf("\n********************************************************************\n* AACPlus v2 Encoder (using Winamp enc_aacplus.dll and nscrt.dll)\n* Coding Technologies encoder 8.0.3\n* Build %s, %s\n********************************************************************\n",
		 __DATE__, __TIME__);
}

void showUsage(_TCHAR* exeName)
{
	_TCHAR exeName2[MAX_PATH];
	_tsplitpath(exeName, NULL, NULL, exeName2, NULL);
	showLogo();
	printf("\nUsage:\n\t%s <wav_file> <bitstream_file> [options]\n", exeName2);
	printf("Options:\n");
	printf("\t--br       - Set bitrate (CBR) to <bitrate> bps. Default is 128000\n");
	printf("\t--mono     - Encode as Mono\n");
	printf("\t--ps       - Enable Parametric Stereo (bitrates up to 56000)\n");
	printf("\t--is       - Independent Stereo, disables Joint Stereo M/S coding\n");
	printf("\t--dc       - Prefer Dual Channels\n");
	printf("\t--he       - Encode as HE-AAC (bitrate up to 128000/213335)\n");
	printf("\t--lc       - Encode as LC-AAC (bitrate up to 320000)\n");
	printf("\t--high     - Encode as HE-AAC with High Bitrates \n\t\t    (bitrates up to 256000, multichannel is not supported)\n");
	printf("\t--speech   - Tune for Speech\n");
	printf("\t--pns      - Enable Perceptual Noise Subsitution (PNS)\n");
	printf("\t--mpeg2aac - Force MPEG2 AAC stream\n");
	printf("\t--mpeg4aac - Force MPEG4 AAC stream\n");
	printf("\t--rawpcm    <rate> <cnt> <bp> - Signal RAW PCM input instead of WAV\n");
	printf("\t\t    <rate> - Samplerate in Hz (32000, 44100 or 48000)\n");
	printf("\t\t    <cnt>  - Channels count (1 or 2)\n");     // and multichannel ?
	printf("\t\t    <bp>   - Bit's per sample (8 or 16)\n");  // 8 not supported in wav ?
	printf("WARNING: this encoder can read and encode data from stdin:\n\t use - as input filename\n");
	printf("The output can be a raw .aac file or a MPEG4 ISO compilant .mp4/.m4a\n(libmp4v2.dll [from Winamp folder] must be in the same directory)\n");
}

int _tmain(int argc, _TCHAR* argv[])
{
	int nChannelCount;
	int nSampleRate;
	int nBitrate=128000;
	int nBitsPerSample=16;
	int nChannelMode=2; //0-Multichannel, 1-Mono, 2-Stereo, 3-IS, 4-PS, 5-DC
	int bMPEG4AAC=9;    // Let the encoder choice: with enc_aacplus.dll 1.24 was 4 but with 1.26 was MPEG2
	int mp4mode = 0;
	int bNoPS = 0;
	int bRawPCM = 0;
	int bSpeech = 0;
	int bPNS = 0;
	int nType=1;        //0- he, 1 - lc, 2 - he-high, 3-PS
	DWORD dwBytesRead;
	_TCHAR szTempDir[MAX_PATH];
	char szTempName[MAX_PATH];
	char lpPathBuffer[BUFSIZE];

	_TCHAR * strOutputFileName = NULL;
	_TCHAR * strInputFileName = NULL;
	FILE*  hInput=NULL;
	FILE*  hOutput=NULL;
	wavHeader wav;

	_tsetlocale(LC_ALL, _T(""));

	if(argc<2)
	{
		showUsage(argv[0]);
		return ERROR_INSUFFICIENT_ARGS;
	}

	strInputFileName	= argv[1];
	strOutputFileName	= argv[2];

	// let's parse command line
	for(int i=3;i<argc;++i)
	{
		if(0==_tcscmp(_T("--rawpcm"), argv[i]))
		{
			nSampleRate=_tstoi(argv[++i]);
			nChannelCount=_tstoi(argv[++i]);
			nBitsPerSample=_tstoi(argv[++i]);
			bRawPCM=1;
			continue;
		}

		if(0==_tcscmp(_T("--mono"), argv[i]))
		{
			nChannelMode=1;
			continue;
		}
		if(0==_tcscmp(_T("--ps"), argv[i]))
		{
			bNoPS=1;
			nType=0;
			//nChannelMode=4; assigned only if bitrate <=56000
			continue;
		}
		if(0==_tcscmp(_T("--is"), argv[i]))
		{
			nChannelMode=3;
			continue;
		}
		if(0==_tcscmp(_T("--dc"), argv[i]))
		{
			nChannelMode=5;
			continue;
		}
		if(0==_tcscmp(_T("--he"), argv[i]))
		{
			nType=0;
			continue;
		}
		if(0==_tcscmp(_T("--lc"), argv[i]))
		{
			nType=1;
			continue;
		}
		if(0==_tcscmp(_T("--high"), argv[i]))
		{
			nType=2;
			continue;
		}
		if(0==_tcscmp(_T("--br"), argv[i]))
		{
			nBitrate=_tstoi(argv[++i]);
			continue;
		}
		if(0==_tcscmp(_T("--mpeg2aac"), argv[i]))
		{
			bMPEG4AAC=0;
			continue;
		}
		if(0==_tcscmp(_T("--mpeg4aac"), argv[i]))
		{
			bMPEG4AAC=1;
			continue;
		}
		if(0==_tcscmp(_T("--speech"), argv[i]))
		{
			bSpeech=1;
			continue;
		}
		if(0==_tcscmp(_T("--pns"), argv[i]))
		{
			bPNS=1;
			continue;
		}
	}

	showLogo();

	int bStdIn = 0==_tcscmp(_T("-"), strInputFileName)?1:0;
	// let's open WAV file
	if(bStdIn)
	{
		#ifdef WIN32
		setmode(fileno(stdin), O_BINARY);
		#endif
		hInput = stdin;
	}
	else
	{
		hInput = _tfopen(strInputFileName,_T("rb"));
	}

	if(!hInput)
	{
		printf("Can't open input file!\n");
		return ERROR_CANNOT_OPEN_INFILE;
	};

	if(0==bRawPCM)
	{
		// let's read WAV HEADER
		memset(&wav, 0, sizeof(wav));
		if(fread(&wav, 1, sizeof(wav), hInput)!=sizeof(wav))
		{
			// Can't read wav header
			fclose(hInput);
			printf("Input file must be WAV PCM!\n");
			return ERROR_CANNOT_LOAD_DECODER;
		}

		if(	wav.chunkid!=0x46464952
			|| wav.rifftype!=0x45564157
			|| wav.fmt!=0x20746D66
			|| wav.fmtsize!=0x00000010
			|| wav.bps!=16
			|| (wav.srate!=32000 && wav.srate!=44100 && wav.srate!=48000 )
			|| wav.ccode!=0x0001)
		{
			// unsupported or invalid wav format
			fclose(hInput);
			printf("Invalid or unsuppored WAV file format (must be 16 bit PCM)\n");
			return ERROR_INCOMPATABLE_DECODER;
		}

		nBitsPerSample	= wav.bps;
		nChannelCount	= wav.nch;
		nSampleRate	= wav.srate;
	}

	int bitrates[3][6] =
	{
		{
		 //he - mono,	 stereo,			6ch
		8000, 64000,	16000, 128000,	96000, 213335
		},
		{
		 //lc - mono,	 stereo,			6ch
		8000, 160000,	16000, 320000,	160000, 320000
		},
		{
		 //hi - mono,	 stereo,			6ch
		64000, 160000,	96000, 256000,	8000, 256000
		}
	};

	int maxBitrate=0;
	int minBitrate=0;

	switch(nChannelCount)
	{
		case 1:
			minBitrate = bitrates[nType][0];
			maxBitrate = bitrates[nType][1];
			break;
		case 2:
			minBitrate = bitrates[nType][2];
			maxBitrate = bitrates[nType][3];
			break;
		default:
			minBitrate = bitrates[nType][4];
			maxBitrate = bitrates[nType][5];
			break;
	}

	const _TCHAR* channelModes[] = {_T("Multichannel"), _T("Mono"), _T("Stereo"), _T("Independant Stereo"), _T("Parametric Stereo"), _T("Dual Channels")};
	const _TCHAR* codecName[] = {_T("HE-AAC"),_T("LC-AAC"),_T("HE-AAC High"),_T("HE-AAC+PS")};
	const _TCHAR* BooleanResult[] = {_T("No"), _T("Yes")};

	nBitrate		= min(max(minBitrate, nBitrate), maxBitrate);

	nChannelMode	= 1==nChannelCount ? 1:((2==nChannelCount) ? ( (nBitrate<=56000 && 0==nType && 1==bNoPS) ? 4:nChannelMode):0);

	if (4==nChannelMode)// only for printf
		nType=3;

	// just to be sure that we have the correct mode
	_TCHAR *ext = PathFindExtension(strOutputFileName);
	if(ext) {
		if(!_tcsicmp(ext,_T(".m4a")) || !_tcsicmp(ext,_T(".mp4"))) mp4mode = 1;
	}

	// let's write used config:
	_tprintf(_T("\nInput file: %s\nOutput file: %s\nSampleRate: %d\nChannelCount: %d\nBitsPerSample: %d\nBitrate: %d\nChannelMode: %s\nEngine: %s\nTune For Speech: %s\nPNS: %s\nMP4 Output: %s\n"),
		strInputFileName, strOutputFileName, nSampleRate, nChannelCount,nBitsPerSample , nBitrate, channelModes[nChannelMode], codecName[nType], BooleanResult[bSpeech?1:0], BooleanResult[bPNS?1:0], BooleanResult[mp4mode?1:0]);
	fflush(stdout);

	if (4==nChannelMode) //back
		nType=0;

	//create temp file name
#ifdef UNICODE
	_TCHAR tempFileW[MAX_PATH];
#endif
	GetTempPath(
		MAX_PATH,   // length of the buffer
        szTempDir);      // buffer for path
#ifdef UNICODE
	GetTempFileName(szTempDir, // directory for temp files
        NULL,                    // temp file name prefix
        0,                        // create unique name
        tempFileW);              // buffer for name
	wcstombs_s(NULL,szTempName,MAX_PATH,tempFileW,(MAX_PATH)*sizeof(_TCHAR));
#else
	GetTempFileName(szTempDir, // directory for temp files
        NULL,                    // temp file name prefix
        0,                        // create unique name
        szTempName);              // buffer for name
#endif

	AudioCoder * encoder=NULL;
	AudioCoder *(*finishAudio3)(_TCHAR *fn, AudioCoder *c)=NULL;
	void (*prepareToFinish)(_TCHAR *filename, AudioCoder *coder)=NULL;
	AudioCoder* (*createAudio3)(int nch, int srate, int bps, unsigned int srct, unsigned int *outt, char *configfile)=NULL;
	HMODULE encplug = LoadLibrary(_T("enc_aacplus.dll"));
	if(NULL == encplug)
	{
		fclose(hInput);
		printf("Can't find enc_aacplus.dll!\n");
		return ERROR_CANNOT_LOAD_ENCODER;
	}
	*(void **)&createAudio3 = (void *)GetProcAddress(encplug, "CreateAudio3");
	if( NULL == createAudio3)
	{
		FreeLibrary(encplug);
		fclose(hInput);
		printf("Can't find CreateAudio3 in enc_aacplus.dll!\n");
		return ERROR_CANNOT_LOAD_DECODER;
	}
#ifdef UNICODE
	*(void **)&finishAudio3=(void *)GetProcAddress(encplug,"FinishAudio3W");
	*(void **)&prepareToFinish=(void *)GetProcAddress(encplug,"PrepareToFinishW");
#else
	*(void **)&finishAudio3=(void *)GetProcAddress(encplug,"FinishAudio3");
	*(void **)&prepareToFinish=(void *)GetProcAddress(encplug,"PrepareToFinish");
#endif

	{
		//const char* codecSection[] = {"audio_aacplus","audio_aac","audio_aacplushigh"};
		FILE * tmp = fopen(szTempName,"wt");
		fprintf(tmp, "[audio%s_aac%s]\nsamplerate=%u\nchannelmode=%u\nbitrate=%u\nv2enable=1\nbitstream=%i\nsignallingmode=0\nspeech=%i\npns=%i\n",
			      mp4mode?"_mp4":"",1==nType?"":2==nType?"plushigh":"plus", nSampleRate, nChannelMode, nBitrate, bMPEG4AAC?5:0, bSpeech?1:0, bPNS?1:0);
		fclose(tmp);

		unsigned int outt = 0;
		if (1==nType)
			outt = mp4mode ? mmioFOURCC('M','4','A',' ') : mmioFOURCC('A','A','C','r');
		else if (2==nType)
			outt = mp4mode ? mmioFOURCC('M','4','A','H') : mmioFOURCC('A','A','C','H');
		else
			outt = mp4mode ? mmioFOURCC('M','4','A','+') : mmioFOURCC('A','A','C','P');
		encoder=createAudio3(nChannelCount,nSampleRate, nBitsPerSample ,mmioFOURCC('P','C','M',' '),&outt,szTempName);
		DeleteFileA(szTempName);
	}


	if(NULL==encoder)
	{
		FreeLibrary(encplug);
		fclose(hInput);
		printf("Can't create encoder!\n");
		return ERROR_CANNOT_OPEN_ENCODER;
	}

	hOutput = _tfopen(strOutputFileName, _T("wb"));

	if (!hOutput)
        {
		delete encoder;
		FreeLibrary(encplug);
		fclose(hInput);
		printf("Can't create output file!\n");
	        return ERROR_CANNOT_OPEN_OUTFILE;
        }

	// encode
	printf("Encoding...");
	int toRead = (2*nChannelCount*2*1024);
	toRead = (sizeof(lpPathBuffer)/toRead)*toRead;
	while(0!=(dwBytesRead = fread(lpPathBuffer, 1, toRead, hInput)))
        {
		_encode(hOutput, encoder, dwBytesRead, lpPathBuffer, bMPEG4AAC);
        }
	printf("\rFinalizing...");

	// finalize encoding
	if(prepareToFinish) prepareToFinish(strOutputFileName,encoder);
	_finalize(hOutput, encoder, lpPathBuffer, bMPEG4AAC);
	fclose(hOutput);
	if (finishAudio3) finishAudio3(strOutputFileName,encoder);
	fclose(hInput);
	delete encoder;
	FreeLibrary(encplug);

	if(mp4mode) {
		struct __stat64 statbuf;
		if(!_tstat64(strOutputFileName,&statbuf)) {
			if(!_tfopen_s(&hOutput,strOutputFileName,_T("r+b"))) {
				optimizeAtoms(hOutput,statbuf.st_size);
			}
			if(hOutput) fclose(hOutput);
		}
	}

	// shut down
	printf("\rDone           \n");
	return TRANSCODE_FINISHED_OK;
}
