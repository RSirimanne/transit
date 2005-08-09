/*
 * dbread_pands.c - Driver to read Partridge & Schwenke for Transit.
 *              Part of Transit program.
 *
 * Copyright (C) 2003 Patricio Rojo (pato@astro.cornell.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <transit.h>
#include <lineread.h>
#include <math.h>

short gabby_dbread=0;

struct textinfo{
  PREC_ZREC **Z;
  PREC_ZREC *T;
  PREC_CS *c;
  PREC_ZREC *mass;
  int nT;
  int nIso;
  char **name;

  long currline;
  char *filename;
};

static int isoname(char ***isotope, int niso);
static FILE *readinfo(char *filename, struct textinfo *textinfo);
static FILE *readlinres(FILE *fp, struct textinfo *textinfo,
			float wlneg, float wlend);
static char *dbname;

#define checkprepost(pointer,pre,omit,post) do{                            \
   if(pre)                                                                 \
     transiterror(TERR_SERIOUS,                                            \
                  "Pre-condition failed on line %i(%s)\n while reading:\n" \
		  "%s\n\nTLI_Ascii format most likely invalid\n"           \
                  ,__LINE__,__FILE__,line);                                \
   while(omit)                                                             \
     pointer++;                                                            \
   if(post)                                                                \
     transiterror(TERR_SERIOUS,                                            \
                  "Post-condition failed on line %i(%s)\n while reading:\n"\
		  "%s\n\nTLI_Ascii format most likely invalid\n"           \
                  ,__LINE__,__FILE__,line);                                \
                                             }while(0)


/* \fcnfh
   It outputs error. Used when EOF is found before expected
*/
static void
earlyend(long lin, char *file)
{
  transiterror(TERR_SERIOUS|TERR_ALLOWCONT,
	       "readlineinfo:: EOF unexpectedly found at line %i in\n"
	       "ascii-TLI linedb info file '%s'\n"
	       ,lin,file);
  exit(EXIT_FAILURE);
}

/**************/

/* \fcnfh
  databasename: Just return the name of the database in a newly
  allocated string.

  @returns -1 on failure
            1 on success
*/
int databasename(char **name)
{

  *name = strdup(dbname);
  return 1;
}

/*********************************************************
 *
 *********************************************************/

/* \fcnfh
   Read line info

   @returns number of lines read
*/
PREC_NREC
dbread_text(char *filename,
	    struct linedb **lines, //2 pointers in order to be
	    //able to allocate memory
	    float wlbeg,           //wavelengths in tli_fct
	    float wlend,           //units
	    /* Partition function data file */
	    char *Zfilename,
	    /* For the following 3 parameter, the memory is
	       allocated in the dbread_* functions, and the
	       size is returned in the last parameters. */
	    PREC_ZREC ***Z,        //Partition function(isot,
	    //temp)
	    PREC_ZREC **T,         //temps for Z
	    PREC_ZREC **isomass,   //Isotopes' mass in AMU
	    int *nT,               //number of temperature
	    //points 
	    int *nIso,             //number of isotopes
	    char ***isonames)      //Isotope's name
{
  struct textinfo texinfo;

  if(Zfilename)
    transiterror(TERR_CRITICAL,
		 "Zfilename needs to be NULL for TLI-ascii file\n");

  FILE *fp = readinfo(filename, &textinfo);

  PREC_NREC ret = readlines(fp, &textinfo, wlbeg, wlend, lines);

  *Z        = textinfo->Z;
  *T        = textinfo->T;
  *isomass  = textinfo->mass;
  *nT       = textinfo->nT;
  *nIso     = textinfo->nIso;
  *isonames = textinfo->name;
  *isocs    = textinfo->cs;

  return ret;
}

/*****************/

/* \fcnfh
 Read info from TLI-ASCII file

 @returns fp of the opened file.
*/
static FILE *
readinfo(char *filename,
	 struct textinfo *textinfo)
{
  FILE *fp = verbfileopen(th->f_line, "TLI-ascii database");
  textinfo->filename = strdup(filename);

  long ndb;
  char line[maxline+1], *lp, *lp2, rc;
  textinfo->currline = 0;

  settoolongerr(&linetoolong,filename,&(textinfo->currline));

  //skip comments and blank lines
  while((rc=fgetupto(line,maxline,fp)) == '#' || rc == '\n')
    textinfo->currline++;
  if(!rc) earlyend(filename, textinfo->currline);

  //get number of database which needs to be one at this point. If
  //omitted number of databases it is assumed to be 1
  ndb = strtol(line, &lp, 0);
  while(isspace(lp++));
  if(*lp) ndb=1;
  else 
    while((rc=fgetupto(line,maxline,fp)) == '#' || rc == '\n')
      textinfo->currline = '\0';
  if(ndb != 1)
    transiterr(TERR_SERIOUS,
	       "TLI-ascii reading by lineread is implemented to read "
	       "only one database per file (%s)."
	       ,filename);
  if(!rc) earlyend(filename, textinfo->currline);

  //read name, number of temps, and number of isotopes
  if((dbname = readstr_sp_alloc(line,&lp,'_'))==NULL)
    transitallocerror(0);
  checkprepost(lp,0,*lp==' '||*lp=='\t',*lp=='\0');
  long nISO, nT, i;
  rn = getnl(2,' ',lp,&nIso,&nT);
  checkprepost(lp,rn!=2,0,0);
  textinfo->nT   = nT;
  textinfo->nIso = nIso;

  //allocate temperature, mass, cross section and partition info
  textinfo->c   = (PREC_CS **)  calloc(nIso,   sizeof(PREC_CS *));
  textinfo->Z   = (PREC_ZREC **)calloc(nIso,   sizeof(PREC_ZREC *));
  textinfo->Z[0]= (PREC_ZREC *) calloc(nIso*nT,sizeof(PREC_ZREC));
  textinfo->c[0]= (PREC_CS *)   calloc(nIso*nT,sizeof(PREC_CS));
  textinfo->name= (char **)     calloc(nIso,   sizeof(char *));
  textinfo->mass= (PREC_ZREC *) calloc(nIso,   sizeof(PREC_ZREC));
  textinfo->T   = (PREC_ZREC *) calloc(nT,     sizeof(PREC_ZREC));

  //get isotope name and mass
  while((rc=fgetupto(lp2=line,maxline,fp)) == '#' || rc == '\n')
    textinfo->currline++;
  if(!rc) earlyend(filename, textinfo->currline);
  for(i=0 ; i<nIso ; i++){
    textinfo->Z[i]=isov->z + nT*i;
    textinfo->c[i]=isov->c + nT*i;
    if((textinfo->names[i]=readstr_sp_alloc(lp2,&lp,'_'))==NULL)
      transitallocerror(0);
    //get mass and convert to cgs
    textinfo->mass[i]=strtod(lp,&lp2);

    if(i!=nIso-1)
      checkprepost(lp2,lp==lp2,*lp2==' '||*lp2=='\t',*lp2=='\0');
  }
  //Last isotope has to be followed by an end of string
  checkprepost(lp2,0,*lp2==' '||*lp2=='\t',*lp2!='\0');

  //get for each temperature a line with temperature, partfcn and
  //cross-sect info
  long t;
  for(t=0 ; t<nT ; t++){
    while((rc=fgetupto(lp=line,maxline,fp)) == '#' || rc == '\n')
      textinfo->currline++;
    if(!rc) earlyend(filename, textinfo->currline);
    while(*lp==' ') lp++;
    textinfo->T[t] = strtod(lp,&lp);
    checkprepost(lp,*lp=='\0',*lp==' '||*lp=='\t',*lp=='\0');

    for(i=0;i<nIso;i++){
      textinfo->Z[i][t]=strtod(lp,&lp);
      checkprepost(lp,*lp=='\0',*lp==' '||*lp=='\t',*lp=='\0');
    }
    for(i=0;i<nIso-1;i++){
      textinfo->c[i][t]=strtod(lp,&lp);
      checkprepost(lp,*lp=='\0',*lp==' '||*lp=='\t',*lp=='\0');
    }
    textinfo->c[i][t]=strtod(lp,&lp);
    checkprepost(lp,0,*lp==' '||*lp=='\t',*lp!='\0');
  }

  return fp;
}

/*****************/

/* \fcnfh
   Read line info


   @returns Error from invalid field
            0 on success
*/
static int
readline(FILE *fp,
	 struct textinfo *textinfo,
	 float wlbeg,
	 float wlend,
	 struct linedb **linesp)
{
  long na=16, nbeg, i=0;
  char *lp, *lp2, rc;

  struct linedb *line = (struct linedb *)calloc(na, sizeof(struct linedb));
  do{
    while((rc=fgetupto(lp=line,maxline,fp)) =='#'||rc=='\n')
      textinfo->currline++;
    //if it is not end of file, read the records.
    if(rc){
      textinfo->currline++;
      if (i>na)
	line = (struct linedb *)realloc(line, (na<<=1) * 
					sizeof(struct linedb));
      double wavl    = strtod(lp, &lp2);
      if (wavl < wlbeg) continue;
      if (wavl > wlend) break;

      line[i].recpos = nbeg+i;
      line[i].wl     = wavl;
      if(lp==lp2) 
	return invalidfield(line, textinfo->filename, textinfo->currline
			    , 1, "central wavelength");
      line[i].isoid=strtol(lp2, &lp, 0);
      if(lp==lp2)
	return invalidfield(line, textinfo->filename, textinfo->currline
			    , 2, "isotope ID");
      line[i].elow=strtod(lp, &lp2);
      if(lp==lp2)
	return invalidfield(line, textinfo->filename, textinfo->currline
			    , 3, "lower energy level");
      line[i++].gf=strtod(lp2, &lp);
      if(lp==lp2)
	return invalidfield(line, textinfo->filename, textinfo->currline
			    , 4, "log(gf)");
    }
  }while(rc);

  *linesp = line;
}

/*
  isoname: returns the name of the isotopes in the newly allocated array
  'isoname'(char ***)

  @todo    error check for calloc calls
  @returns 1 on success
*/
static int isoname(char ***isonames, int niso)
{
  int i;

  *isonames=(char **)calloc(niso,sizeof(char *));
  for(i=0;i<niso;i++){
    (*isonames)[i]=(char *)calloc(strlen(isotope[i])+1,sizeof(char));
    strcpy((*isonames)[i],isotope[i]);
  }

  return 1;
  
}

