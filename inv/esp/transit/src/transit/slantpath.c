/*
 * slantpath.c
 * slantpath.txc - Functions to handle a slant path problem. Component
 *                 of the transit program
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <transit.h>

/***** Warning:
       To speed up, slantpath.txc assumed version 1.4 of gsl
       utilities. If not, structure gsl_interp_accel might have
       different components, which will produce erroneus assumptions in
       totaltau(). 
***************/


/* \fcnfh
 Computes optical depth at a given impact parameter, note that b needs
 to be given in units of 'rad' and the result needs to be multiplied by
 the units 'rad' to be real.
 There is no ray bending, refr=constant

 @returns $\frac{tau}{units_{rad}}$ returns optical depth divided by units
                                    of 'rad'
*/
static inline PREC_RES
totaltau1(PREC_RES b,		/* impact parameter */
	  PREC_RES *rad,	/* Equispaced radius array */
	  PREC_RES refr,	/* refractivity index */
	  PREC_RES *ex,		/* extinction[rad] */
	  long nrad)		/* number of radii elements */
{
  int rs;
  int i;
  PREC_RES res;

  //Look for closest approach radius
  PREC_RES r0=b/refr;

  //get bin value 'rs' such that r0 is between rad[rs] inclusive
  //and rad[rs+1] exclusive.
  //If we are looking at the outmost layer, then return
  if((rs=binsearch(rad,0,nrad-1,r0))==-5)
    return 0;
  //If some other error occurred
  else if(rs<0)
    transiterror(TERR_CRITICAL,
		 "Closest approach value(%g) is outside sampled radius\n"
		 "range(%g - %g)\n"
		 ,r0,rad[0],rad[nrad-1]);
  //advance the extinction and radius arrays such that the zeroeth
  //element now is the closest sample to the minimum approach from below
  //it.
  rad+=rs;
  ex+=rs;
  nrad-=rs;

  //By parabola fitting, interpolate the value of extinction at the
  //radius of minimum approach and store it in the sample that
  //corresponded to the closest from below. Store such value and radius,
  //which are to be replaced before returning (\lin{tmpex})
  const PREC_RES tmpex=*ex;
  const PREC_RES tmprad=*rad;
  if(nrad==2) *ex=interp_parab(rad-1,ex-1,r0);
  else *ex=interp_parab(rad,ex,r0);
  *rad=r0;
  const PREC_RES dr=rad[1]-rad[0];

  //Now convert to s spacing, i.e. distance along the path. Radius needs
  //to be equispaced.
  //If we are not in the border
  if(nrad>2){
    PREC_RES s[nrad];
    const PREC_RES Dr=rad[2]-rad[1];
    const PREC_RES cte=dr*(dr + 2*r0);
    s[0]=0;

    for(i=1 ; i<nrad ; i++)
      s[i]=sqrt(cte + (i-1)*Dr*(2.0*(r0+dr) + (i-1)*Dr) );

    //Integrate!\par
    //Use spline if GSL is available along with at least 3 points
#ifdef _USE_GSL
    gsl_interp_accel acc={0,0,0};
    gsl_interp *spl=gsl_interp_alloc(gsl_interp_cspline,nrad);
    gsl_interp_init(spl,s,ex,nrad);
    res=gsl_interp_eval_integ(spl,s,ex,0,s[nrad-1],&acc);
    gsl_interp_free(spl);
#else
#error non equispaced integration is not implemented without GSL
#endif /* _USE_GSL */
  }

  //Integrate towards a constant value of extinction if we are in the
  //outmost layer
  else{
    if(ex[1]==ex[0])
      res= ex[0] * r0 * ( sqrt( rad[1] * rad[1] / r0 / r0 - 1) );
    else{
      PREC_RES alpha = ( ex[1] - ex[0] ) / dr;
      PREC_RES rm    = rad[1];
      if(alpha<0)
	res= - alpha * (rm * sqrt( rm * rm - r0 * r0) - r0 * r0 * 
			log( sqrt( rm * rm / r0 / r0 - 1) + rm / r0 ) )
	  / 2.0;
      else
	res=   alpha * (rm * sqrt( rm * rm - r0 * r0) + r0 * r0 * 
			log( sqrt( rm * rm / r0 / r0 - 1) + rm / r0 ) )
	  / 2.0;
    }
  }

  //replace original value of extinction
  //\linelabel{tmpex}
  *ex=tmpex;
  *rad=tmprad;

  //return
  return 2*(res);
}


/* \fcnfh
 Computes optical depth at a given impact parameter, note that b needs
 to be given in units of 'rad' and the result needs to be multiplied by
 the units 'rad' to be real. 
 This uses a bent path ray solution.

 @returns $\frac{tau}{units_{rad}}$ returns optical depth divided by units
                                    of 'rad'
*/
static inline PREC_RES
totaltau2(PREC_RES b,		/* differential impact parameter with
				   respect to maximum value */
	  PREC_RES *rad,	/* radius array */
	  PREC_RES *refr,	/* refractivity index */
	  PREC_RES *ex,		/* extinction[rad] */
	  long nrad)		/* number of radii elements */
{
  PREC_RES dt[nrad];
  PREC_RES r0a=b;
  PREC_RES r0=0;
  int i;
  const int maxiterations=50;
  int rs;

  //Look for closest approach radius
  i=0;
  while(1){
    r0=b/lineinterp(r0a,rad,refr,nrad);
    if(r0==r0a)
      break;
    if(i++>maxiterations)
      transiterror(TERR_CRITICAL,
		   "Maximum iterations(%i) reached while looking for\n"
		   "r0. Convergence not reached (%.6g!=%.6g)\n"
		   ,maxiterations,r0,r0a);
    r0a=r0;
  }

  //get bin value 'rs' such that r0 is between rad[rs-1] inclusive
  //and rad[rs] exclusive.
  //If we are looking at the outmost layer, then return
  if((rs=binsearch(rad,0,nrad-1,r0))==-5)
    return 0;
  //If some other error occurred
  else if(rs<0)
    transiterror(TERR_CRITICAL,
		 "Closest approach value(%g) is outside sampled radius\n"
		 "range(%g - %g)\n"
		 ,r0,rad[0],rad[nrad-1]);
  //advance the index to point as desired. Now nrad-rs indicates the
  //number of points available for integration.
  rs++;

  //A fraction 'analiticfrac' of the integration near the closest
  //appraoach is calcualated analitically, otherwise, I get a division
  //by zero. In formula\par
  //\[
  //\tau_{\wn}(\rho)=
  //\underbrace{
  //\frac{2\extc_{\wn}\rho}{n}\left(
  //                   \sqrt{\left(\frac{nr_1}{\rho}\right)^2-1}
  //                  -\sqrt{\left(\frac{nr_0}{\rho}\right)^2-1}\right) 
  //}_{\mathrm{analitic}} +
  //\underbrace{
  //2\int_{r_1=r_0+\delta r}^{\infty}
  //\frac{\extc_{\wn}~n~r}{\sqrt{n^2r^2-\rho^2}}\dd r
  //}_{\mathrm{numerical}}
  //\]\par
  //First for the analitical part of the integral
  PREC_RES res;
  if(ex[rs-1]==ex[rs])
    res= ex[rs] * r0 * ( sqrt( rad[rs] * rad[rs] / r0 / r0 - 1) );
  else{
    PREC_RES alpha = ( ex[rs] - ex[rs-1] ) / ( rad[rs] - rad[rs-1] );
    PREC_RES rm    = rad[rs];
    if(alpha<0)
      res= - alpha * (rm * sqrt( rm * rm - r0 * r0) - r0 * r0 * 
		      log( sqrt( rm * rm / r0 / r0 - 1) + rm / r0 ) )
	/ 2.0;
    else
      res=   alpha * (rm * sqrt( rm * rm - r0 * r0) + r0 * r0 * 
		      log( sqrt( rm * rm / r0 / r0 - 1) + rm / r0 ) )
	/ 2.0;
  }

  //And now for the numerical integration. Set the variables
  for(i=rs;i<nrad;i++){
    r0a=b/refr[i]/rad[i];
    transitASSERT(r0a>1,
		  "Oops! assert condition not met, b/(nr)=%g > 1\n"
		  ,r0a);

    dt[i]=ex[i]/sqrt(1-r0a*r0a);
  }

  //Integrate!\par
  //Use spline if GSL is available along with at least 3 points
#ifdef _USE_GSL
  gsl_interp_accel acc={0,0,0};
  if(nrad-rs>2){
    gsl_spline *spl=gsl_spline_alloc(gsl_interp_cspline,nrad-rs);
    gsl_spline_init(spl,rad+rs,dt+rs,nrad-rs);
    res+=gsl_spline_eval_integ(spl,rad[rs],rad[nrad-1],&acc);
    gsl_spline_free(spl);
  }
  //Only integrate Trapezium if there is only two points available.
  else
#endif /* _USE_GSL */
  //Integrate Simpson-Trapezium if enough(w/o GSL) or not enough(w/ GSL)
  //elements. 
  if(nrad-rs>1)
    res+=integ_trasim(rad[1]-rad[0],dt+rs,nrad-rs);

  return 2*(res);
}


/* \fcnfh
   Computes most basic modulation scheme, obtained when there is no limb
   darkening or emitted flux 

   @returns modulation obtained
*/
static inline PREC_RES
modulation1 (PREC_RES *tau,
	     long last,
	     double toomuch,
	     prop_samp *ip,	/* Order is descending */
	     struct geometry *sg)
{
  //general variables
  PREC_RES res;
  double srad=sg->starrad*sg->starradfct;

  //Impact parameter variables
  long ipn=ip->n;
  long i;

  PREC_RES rinteg[ipn],ipv[ipn];

  //this function calculates 1 minus the ratio of in-transit over out-of-transit
  //expresion for the simplest case, which is given by (INVALID EXPRESSION!!)
  //\[
  //M_{\lambda}^{(0)}=
  //\frac{1}{\pi R_M^2}\int_{R<R_M}\ee^{-\tau( b,\xi)} R \dd R\dd\phi
  //\]\par
  //Let's integrate; for each of the planet's layer starting from the
  //outermost until the closest layer
  for(i=0;i<=last;i++){
    ipv[i] = ip->v[last-i] * ip->fct;

    rinteg[i] = exp(-tau[i]) * ipv[i];
  }
  //fill two more lower part bins with 0. Only two to have a nice ending
  //spline and not unnecessary values.
  last+=2;
  if(last>ipn-1) last=ipn-1;
  for(;i<last;i++){
    ipv[i]    = ip->v[last-i] * ip->fct;
    rinteg[i] = 0;
  }

  //increment last to represent number of elements now, check that we
  //have enough.
  last++;
  if(last<3)
    transiterror(TERR_CRITICAL,
		 "Condition failed, less than 3 items (only %i) for radial\n"
		 "integration.\n"
		 ,last);

  //integrate in radii
#ifdef _USE_GSL
  gsl_interp_accel acc={0,0,0};
  gsl_interp *spl=gsl_interp_alloc(gsl_interp_cspline,last);
  gsl_interp_init(spl,ipv,rinteg,last);
  res=gsl_interp_eval_integ(spl,ipv,rinteg,ipv[0],ipv[last-1],&acc);
  gsl_interp_free(spl);

  //or err without GSL
#else
# error computation of modulation() without GSL is not implemented
#endif

  /* TD: Add real unblocked area of the star, considering geometry */
  //substract the total area blocked by the planet. This is from the
  //following
  //\begin{align}
  //1-M=&1-\frac{\int_0^{r_p}\int\ee^{-\tau}\dd \theta r\dd r
  //             +\int_{r_p}^{R_s}\dd A}
  //            {\pi R_s^2}\\
  //   =&\frac{\int_0^{r_p}\int\ee^{-\tau}\dd \theta r\dd r
  //           -Area_{planet}}
  //          {\pi R_s^2}
  //\end{align}
  res = 2.0 * res 
    + srad * srad - ipv[last-1] * ipv[last-1]
    + exp(-toomuch) * ipv[0] * ipv[0];

  res *= 1.0 / srad / srad;

  return res;
}


/* \fcnfh
 Computes optical depth at a given impact parameter, note that b needs
 to be given in units of 'rad' and the result needs to be multiplied by
 the units 'rad' to be real.

 @returns $\frac{tau}{units_{rad}}$ returns optical depth divided by units
                                    of 'rad'
*/
static inline PREC_RES
totaltau(PREC_RES b,		/* differential impact parameter with
				   respect to maximum value */
	 PREC_RES *rad,		/* radius array */
	 PREC_RES *refr,	/* refractivity index */
	 PREC_RES *ex,		/* extinction[rad] */
	 long nrad,		/* number of radii elements */
	 int exprlevel)		/* Expression level of detail */
{
  switch(exprlevel){
  case 1:
    return totaltau1(b,rad,*refr,ex,nrad);
    break;
  case 2:
    return totaltau2(b,rad,refr,ex,nrad);
    break;
  default:
    transiterror(TERR_CRITICAL,
		 "slantpath:: totaltau:: Level %i of detail\n"
		 "has not been implemented to compute optical depth\n"
		 ,exprlevel);
    return 0;
  }
}




/* \fcnfh 
   observable information as it would be seen before any telescope
   interaction. Calculates ratio in-transit over out-transit for the
   simplest expression of modulation.

   @returns modulation obtained
*/
static PREC_RES
modulationperwn (PREC_RES *tau,
		 long last,
		 double toomuch,
		 prop_samp *ip,
		 struct geometry *sg,
		 int exprlevel)
{
  switch(exprlevel){
  case 1:
    return modulation1(tau,last,toomuch,ip,sg);
    break;
  default:
    transiterror(TERR_CRITICAL,
		 "slantpath:: modulationperwn:: Level %i of detail\n"
		 "has not been implemented to compute modulation\n"
		 ,exprlevel);
    return 0;
  }
}


const transit_ray_solution slantpath =
  {
    "Slant Path",		/* Name of the solution */
    "slantpath.c",		/* This file name */
    "1.5",			/* GSL version */
    1,				/* Equispaced impact parameter requested? */
    &totaltau,			/* per impact parameter and per
				   wavenumber value computation */
    &modulationperwn,		/* per wavenumber value computation in
				   its simplest expression */
    1				/* Number of levels of modulation detail
				   as it can be handled by the above
				   fucntion */
  };
