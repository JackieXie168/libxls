/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * This file is part of libxls -- A multiplatform, C library
 * for parsing Excel(TM) files.
 *
 * libxls is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libxls is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libxls.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Copyright 2004 Komarov Valery
 * Copyright 2006 Christophe Leitienne
 * Copyright 2008 David Hoerl
 */
 
//#include <malloc.h>
#include <memory.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <libxls/ole.h>
#include <libxls/xlstool.h>

extern int xls_debug;

const DWORD MSATSECT =		0xFFFFFFFC;	// -4
const DWORD FATSECT =		0xFFFFFFFD;	// -3
const DWORD ENDOFCHAIN =	0xFFFFFFFE;	// -2
const DWORD FREESECT =		0xFFFFFFFF;	// -1


void ole2_bufread(OLE2Stream* olest) 
{
	BYTE *ptr;
	size_t ret;

assert(olest);
assert(olest->ole);

    if (olest->fatpos!=ENDOFCHAIN)
    {
		if(olest->sfat) {
assert(olest->ole->SSAT);
assert(olest->buf);
assert(olest->ole->SSecID);

			ptr = olest->ole->SSAT + olest->fatpos*olest->ole->lssector;
			memcpy(olest->buf, ptr, olest->bufsize); 

			olest->fatpos=olest->ole->SSecID[olest->fatpos];
			olest->pos=0;
			olest->cfat++;
		} else {
			fseek(olest->ole->file,olest->fatpos*olest->ole->lsector+512,0);
			ret = fread(olest->buf,1,olest->bufsize,olest->ole->file);
			assert(ret == olest->bufsize);

			olest->fatpos=olest->ole->SecID[olest->fatpos];
			olest->pos=0;
			olest->cfat++;
		}
    }
	// else printf("ENDOFCHAIN!!!\n");
}

int ole2_read(void* buf,long size,long count,OLE2Stream* olest)
{
    long didReadCount=0;
    long totalReadCount;;	// was DWORD
	unsigned long needToReadCount;

	totalReadCount=size*count;

	// olest->size inited to -1
	//printf("===== ole2_read(%ld bytes)\n", totalReadCount);

    if (olest->size>=0 && !olest->sfat)	// directory is -1
    {
		int rem;
		rem = olest->size - (olest->cfat*olest->ole->lsector+olest->pos);		
        totalReadCount = rem<totalReadCount?rem:totalReadCount;
        if (rem<=0) olest->eof=1;

		// printf("  rem=%ld olest->size=%d - subfunc=%d\n", rem, olest->size, (olest->cfat*olest->ole->lsector+olest->pos) );
	}
	//printf("  totalReadCount=%d (rem=%d size*count=%ld)\n", totalReadCount, rem, size*count);

	while ((!olest->eof) && (didReadCount!=totalReadCount))
	{
		unsigned long remainingBytes;

		needToReadCount	= totalReadCount - didReadCount;
		remainingBytes	= olest->bufsize - olest->pos;
		//printf("  test: (totalReadCount-didReadCount)=%d (olest->bufsize-olest->pos)=%d\n", (totalReadCount-didReadCount), (olest->bufsize-olest->pos) );

		if (needToReadCount < remainingBytes)	// does the current sector contain all the data I need?
		{
			// printf("  had %d bytes of memory, copy=%d\n", (olest->bufsize-olest->pos), needToReadCount);
			memcpy((BYTE*)buf + didReadCount, olest->buf + olest->pos, needToReadCount);
			olest->pos		+= needToReadCount;
			didReadCount	+= needToReadCount;
		} else {
			// printf("  had %d bytes of memory, copy=%d\n", remainingBytes, remainingBytes);
			memcpy((BYTE*)buf + didReadCount, olest->buf + olest->pos, remainingBytes);
			olest->pos		+= remainingBytes;
			didReadCount	+= remainingBytes;
			ole2_bufread(olest);
		}
		assert(didReadCount <= totalReadCount);
		//printf("  if(fatpos=0x%X==EOC=0x%X) && (pos=%d >= bufsize=%d)\n", olest->fatpos, ENDOFCHAIN, olest->pos, olest->bufsize);
		if ((olest->fatpos == ENDOFCHAIN) && (olest->pos >= olest->bufsize))
		{
			olest->eof=1;
		}

		//printf("  eof=%d (didReadCount=%ld != totalReadCount=%ld)\n", olest->eof, didReadCount, totalReadCount);
	}
	// printf("  didReadCount=%ld EOF=%d\n", didReadCount, olest->eof);
	// printf("=====\n");
    return(didReadCount);
}

OLE2Stream* ole2_sopen(OLE2* ole,DWORD start, int size)
{
    OLE2Stream* olest=NULL;

	olest=(OLE2Stream*)calloc(1, sizeof(OLE2Stream));
	olest->ole=ole;
	olest->size=size;
	olest->fatpos=start;
	olest->start=start;
	olest->pos=0;
	olest->eof=0;
	olest->cfat=-1;
	if(size > 0 && size < (int)ole->sectorcutoff) {
		olest->bufsize=ole->lssector;
		olest->sfat = 1;
	} else {
		olest->bufsize=ole->lsector;
	}
	olest->buf=malloc(olest->bufsize);
	ole2_bufread(olest);

	// if(xls_debug) printf("sopen: sector=%d next=%d\n", start, olest->fatpos);
    return olest;
}

void ole2_seek(OLE2Stream* olest,DWORD ofs)
{
	if(olest->sfat) {
		ldiv_t div_rez=ldiv(ofs,olest->ole->lssector);
		int i;
		olest->fatpos=olest->start;

		if (div_rez.quot!=0)
		{
			for (i=0;i<div_rez.quot;i++)
				olest->fatpos=olest->ole->SSecID[olest->fatpos];
		}

		ole2_bufread(olest);
		olest->pos=div_rez.rem;
		olest->eof=0;
		olest->cfat=div_rez.quot;
		//printf("%i=%i %i\n",ofs,div_rez.quot,div_rez.rem);
	} else {
		ldiv_t div_rez=ldiv(ofs,olest->ole->lsector);
		int i;
		olest->fatpos=olest->start;

		if (div_rez.quot!=0)
		{
			for (i=0;i<div_rez.quot;i++)
				olest->fatpos=olest->ole->SecID[olest->fatpos];
		}

		ole2_bufread(olest);
		olest->pos=div_rez.rem;
		olest->eof=0;
		olest->cfat=div_rez.quot;
		//printf("%i=%i %i\n",ofs,div_rez.quot,div_rez.rem);
	}
}

OLE2Stream*  ole2_fopen(OLE2* ole,char* file)
{
    OLE2Stream* olest;
    int i;
    for (i=0;i<ole->files.count;i++) {
		char *str = ole->files.file[i].name;
        if (str && strcmp(str,file)==0)	// newer versions of Excel don't write the "Root Entry" string for the first set of data
        {
            olest=ole2_sopen(ole,ole->files.file[i].start,ole->files.file[i].size);
            return(olest);
        }
	}
    return(NULL);
}

OLE2* ole2_open(char *file, char *charset)
{
    //BYTE buf[1024];
    OLE2Header* oleh;
    OLE2* ole;
    OLE2Stream* olest;
    PSS*	pss;
    char* name = NULL;
    int count,i;

	if(xls_debug) printf("ole2_open: %s\n", file);
    oleh=(OLE2Header*)malloc(512);
    ole=(OLE2*)calloc(1, sizeof(OLE2));
    if (!(ole->file=fopen(file,"rb")))
    {
        if(xls_debug) printf("File not found\n");
        free(ole);
        return(NULL);
    }
    fread(oleh,1,512,ole->file);

	// make sure the file looks good. Note: this code only works on Little Endian machines
	if(oleh->id[0] != 0xE011CFD0 || oleh->id[1] != 0xE11AB1A1 || oleh->byteorder != 0xFFFE) {
		fclose(ole->file);
		free(ole);
		return NULL;
	}

    //ole->lsector=(WORD)pow(2,oleh->lsector);
    //ole->lssector=(WORD)pow(2,oleh->lssector);
	ole->lsector=512;
    ole->lssector=64;
	assert(oleh->lsectorB==9);	// 2**9 == 512
	assert(oleh->lssectorB==6);	// 2**6 == 64
	
    ole->cfat=oleh->cfat;
    ole->dirstart=oleh->dirstart;
    ole->sectorcutoff=oleh->sectorcutoff;
    ole->sfatstart=oleh->sfatstart;
    ole->csfat=oleh->csfat;
    ole->difstart=oleh->difstart;
    ole->cdif=oleh->cdif;
    ole->files.count=0;

	if(xls_debug) {
		printf("==== OLE HEADER ====\n");
		//printf ("Header Size:   %i \n", sizeof(OLE2Header));
		//printf ("id[0]-id[1]:   %X-%X \n", oleh->id[0], oleh->id[1]);
		printf ("verminor:      %X \n",oleh->verminor);
		printf ("verdll:        %X \n",oleh->verdll);
		//printf ("Byte order:    %X \n",oleh->byteorder);
		printf ("sect len:      %X (%i)\n",ole->lsector,ole->lsector);		// ole
		printf ("mini len:      %X (%i)\n",ole->lssector,ole->lssector);	// ole
		printf ("Fat sect.:     %i \n",oleh->cfat);
		printf ("Dir Start:     %i \n",oleh->dirstart);
		
		printf ("Mini Cutoff:   %i \n",oleh->sectorcutoff);
		printf ("MiniFat Start: %X \n",oleh->sfatstart);
		printf ("Count MFat:    %i \n",oleh->csfat);
		printf ("Dif start:     %X \n",oleh->difstart);
		printf ("Count Dif:     %i \n",oleh->cdif);
		printf ("Fat Size:      %i (0x%X) \n",oleh->cfat*ole->lsector,oleh->cfat*ole->lsector);
	}
	assert(ole->cfat <= 109);	// current limitation

	// allocate a contiguous set of sector sized objects, that you can then later index through
    ole->SecID = malloc(ole->cfat*ole->lsector);
	// read in the desired sectors
    count=(ole->cfat<109)?ole->cfat:108;
    for (i=0;i<count;i++)
    {
		// if(xls_debug) printf("MSAT[%d] -> sector %d\n", i, oleh->MSAT[i]);
		// oleh points to the entries located within the initial header
		fseek(ole->file,oleh->MSAT[i]*ole->lsector+512,0);
		fread((BYTE*)ole->SecID+i*ole->lsector,1,ole->lsector,ole->file);
    }
#if 0
	if(xls_debug) {
		//printf("==== READ IN SECTORS FOR MSAT TABLE====\n");
		for(i=0; i<512/4; ++i) {	// just the first block
			if(ole->SecID[i] != FREESECT) printf("SecID[%d]=%d\n", i, ole->SecID[i]);
		}
	}
#endif

	// read in short table
	if(ole->sfatstart != ENDOFCHAIN) {
		DWORD sector, k;
		BYTE *wptr;
		
		ole->SSecID = (DWORD *)malloc(ole->csfat*ole->lsector);
		sector = ole->sfatstart;
		wptr=(BYTE*)ole->SSecID;
		for(k=0; k<ole->csfat; ++k) {
			assert(sector != ENDOFCHAIN);
			fseek(ole->file,sector*ole->lsector+512,0);
			fread(wptr,1,ole->lsector,ole->file);
			wptr += ole->lsector;
			sector = ole->SecID[sector];
		}
#if 0
		if(xls_debug) {
			for(i=0; i<512/4; ++i) {
				if(ole->SSecID[i] != FREESECT) printf("SSecID[%d]=%d\n", i, ole->SSecID[i]);
			}
		}
#endif
	}

	// reuse this buffer
    pss = (PSS*)oleh;
	oleh = (void *)NULL;
	
    olest=ole2_sopen(ole,ole->dirstart, -1);
    do
    {
        ole2_read(pss,1,sizeof(PSS),olest);

        name=utf8_decode(pss->name, pss->bsize, 0, charset);
        if (pss->type == PS_USER_ROOT || pss->type == PS_USER_STREAM) // (name!=NULL) // 
        {
            if (ole->files.count==0)
            {
                ole->files.file=malloc(sizeof(struct st_olefiles_data));
            } else {
                ole->files.file=realloc(ole->files.file,(ole->files.count+1)*sizeof(struct st_olefiles_data));
            }
            ole->files.file[ole->files.count].name=name;
            ole->files.file[ole->files.count].start=pss->sstart;
            ole->files.file[ole->files.count].size=pss->size;
            ole->files.count++;
			
			if(pss->sstart == ENDOFCHAIN) {
				if (xls_debug) verbose("END OF CHAIN\n");
			} else
			if(pss->type == PS_USER_STREAM) {
				if(xls_debug) {
					printf("----------------------------------------------\n");
					printf("name: %s (size=%d [c=%c])\n", name, pss->bsize, name ? name[0]:' '); // Russian "cp866"
					printf("bsize %i\n",pss->bsize);
					printf("type %i\n",pss->type);
					printf("flag %i\n",pss->flag);
					printf("left %X\n",pss->left);
					printf("right %X\n",pss->right);
					printf("child %X\n",pss->child);
					printf("guid %.4X-%.4X-%.4X-%.4X %.4X-%.4X-%.4X-%.4X\n",pss->guid[0],pss->guid[1],pss->guid[2],pss->guid[3],
						pss->guid[4],pss->guid[5],pss->guid[6],pss->guid[7]);
					printf("user flag %.4X\n",pss->userflags);
					printf("sstart %.4d\n",pss->sstart);
					printf("size %.4d\n",pss->size);
				}
			} else
			if(pss->type == PS_USER_ROOT) {
				DWORD sector, k, blocks;
				BYTE *wptr;
				
				blocks = (pss->size + (ole->lsector - 1)) / ole->lsector;	// count partial
				ole->SSAT = (BYTE *)malloc(blocks*ole->lsector);
				// printf("blocks %d\n", blocks);

				assert(ole->SSecID);
				
				sector = pss->sstart;
				wptr=(BYTE*)ole->SSAT;
				for(k=0; k<blocks; ++k) {
					// printf("block %d sector %d\n", k, sector);
					assert(sector != ENDOFCHAIN);
					fseek(ole->file,sector*ole->lsector+512,0);
					fread(wptr,1,ole->lsector,ole->file);
					wptr += ole->lsector;
					sector = ole->SecID[sector];
				}
			}	
		}
    }
    while (!olest->eof);
    free(olest);

    return ole;
}

void ole2_close(OLE2* ole2)
{
	fclose(ole2->file);
	free(ole2);
}

void ole2_fclose(OLE2Stream* ole2st)
{
	free(ole2st);
}
