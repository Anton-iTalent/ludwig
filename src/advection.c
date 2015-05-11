/*****************************************************************************
 *
 *  advection.c
 *
 *  Computes advective order parameter fluxes from the current
 *  velocity field (from hydrodynamics) and the the current
 *  order parameter(s).
 *
 *  Fluxes are all computed at the interface of the control cells
 *  surrounding each lattice site. Unique face fluxes guarantee
 *  conservation of the order parameter.
 *
 *  To deal with Lees-Edwards boundaries positioned at x = constant
 *  we have to allow the 'east' face flux to be stored separately
 *  to the 'west' face flux. There's no effect in the y- or z-
 *  directions.
 *
 *  Any solid-fluid boundary conditions are dealt with post-hoc by
 *  in advection_bcs.c
 *
 *  $Id$
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *  (c) 2010 The University of Edinburgh
 *
 *****************************************************************************/

#include <assert.h>
#include <stdlib.h>

#include "pe.h"
#include "coords.h"
#include "leesedwards.h"
#include "field_s.h"
#include "advection_s.h"
#include "psi_gradients.h"
#include "hydro_s.h"

static int advection_le_1st(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f);
static int advection_le_2nd(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f);
static int advection_le_3rd(advflux_t * flux, hydro_t * hydro, int nf,
			    field_t * field);
static int advection_le_4th(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f);
static int advection_le_5th(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f);

static int order_ = 1; /* Default is upwind (bad!) */

/*****************************************************************************
 *
 *  advection_order_set
 *
 *****************************************************************************/

int advection_order_set(const int n) {

  order_ = n;
  return 0;
}

/*****************************************************************************
 *
 *  advection_order
 *
 *****************************************************************************/

int advection_order(int * order) {

  assert(order);

  *order = order_;

  return 0;
}

/*****************************************************************************
 *
 *  advflux_create
 *
 *****************************************************************************/

int advflux_create(int nf, advflux_t ** pobj) {

  int nsites;
  advflux_t * obj = NULL;

  assert(pobj);

  obj = (advflux_t*) calloc(1, sizeof(advflux_t));
  if (obj == NULL) fatal("calloc(advflux) failed\n");

  nsites = le_nsites();

  obj->fe = (double*) calloc(nsites*nf, sizeof(double));
  obj->fw = (double*) calloc(nsites*nf, sizeof(double));
  obj->fy = (double*) calloc(nsites*nf, sizeof(double));
  obj->fz = (double*) calloc(nsites*nf, sizeof(double));

  if (obj->fe == NULL) fatal("calloc(advflux->fe) failed\n");
  if (obj->fw == NULL) fatal("calloc(advflux->fw) failed\n");
  if (obj->fy == NULL) fatal("calloc(advflux->fy) failed\n");
  if (obj->fz == NULL) fatal("calloc(advflux->fz) failed\n");


  /* allocate target copy of structure */
  targetMalloc((void**) &(obj->tcopy),sizeof(advflux_t));

  /* allocate data space on target */
  double* tmpptr;

  advflux_t* t_obj = obj->tcopy;

  //fe
  targetCalloc((void**) &tmpptr,nf*nsites*sizeof(double));
  copyToTarget(&(t_obj->fe),&tmpptr,sizeof(double*)); 

  //fw
  targetCalloc((void**) &tmpptr,nf*nsites*sizeof(double));
  copyToTarget(&(t_obj->fw),&tmpptr,sizeof(double*)); 

  //fy
  targetCalloc((void**) &tmpptr,nf*nsites*sizeof(double));
  copyToTarget(&(t_obj->fy),&tmpptr,sizeof(double*)); 

  //fz
  targetCalloc((void**) &tmpptr,nf*nsites*sizeof(double));
  copyToTarget(&(t_obj->fz),&tmpptr,sizeof(double*)); 

  *pobj = obj;

  return 0;
}

/*****************************************************************************
 *
 *  advflux_free
 *
 *****************************************************************************/

void advflux_free(advflux_t * obj) {

  assert(obj);

  free(obj->fe);
  free(obj->fw);
  free(obj->fy);
  free(obj->fz);



  //free data space on target 
  double* tmpptr;
  advflux_t* t_obj = obj->tcopy;
  copyFromTarget(&tmpptr,&(t_obj->fe),sizeof(double*)); 
  targetFree(tmpptr);
  copyFromTarget(&tmpptr,&(t_obj->fw),sizeof(double*)); 
  targetFree(tmpptr);
  copyFromTarget(&tmpptr,&(t_obj->fy),sizeof(double*)); 
  targetFree(tmpptr);
  copyFromTarget(&tmpptr,&(t_obj->fz),sizeof(double*)); 
  targetFree(tmpptr);

  //free target copy of structure
  targetFree(obj->tcopy);


  free(obj);

  return;
}

/*****************************************************************************
 *
 *  advection_x
 *
 *****************************************************************************/

int advection_x(advflux_t * obj, hydro_t * hydro, field_t * field) {

  int nf;

  assert(obj);
  assert(hydro);
  assert(field);

  field_nf(field, &nf);

  /* For given LE , and given order, compute fluxes */


  switch (order_) {
  case 1:
    advection_le_1st(obj, hydro, nf, field->data);
    break;
  case 2:
    advection_le_2nd(obj, hydro, nf, field->data);
    break;
  case 3:
    advection_le_3rd(obj, hydro, nf, field);
    break;
  case 4:
    advection_le_4th(obj, hydro, nf, field->data);
    break;
  case 5:
    advection_le_5th(obj, hydro, nf, field->data);
    break; 
  default:
    fatal("Unexpected advection scheme order\n");
  }

  return 0;
}

/*****************************************************************************
 *
 *  advection_le_1st
 *
 *  The advective fluxes are computed via first order upwind
 *  allowing for LE planes.
 * 
 *  The following are set (as for all the upwind routines):
 *
 *  fluxw  ('west') is the flux in x-direction between cells ic-1, ic
 *  fluxe  ('east') is the flux in x-direction between cells ic, ic+1
 *  fluxy           is the flux in y-direction between cells jc, jc+1
 *  fluxz           is the flux in z-direction between cells kc, kc+1
 *
 *****************************************************************************/

static int advection_le_1st(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f) {
  int nlocal[3];
  int ic, jc, kc;            /* Counters over faces */
  int index0, index1, n;
  int icm1, icp1;
  double u0[3], u1[3], u;
  double phi0;

  assert(flux);
  assert(hydro);
  assert(f);

  coords_nlocal(nlocal);

  for (ic = 1; ic <= nlocal[X]; ic++) {
    icm1 = le_index_real_to_buffer(ic, -1);
    icp1 = le_index_real_to_buffer(ic, +1);
    for (jc = 0; jc <= nlocal[Y]; jc++) {
      for (kc = 0; kc <= nlocal[Z]; kc++) {

	index0 = le_site_index(ic, jc, kc);

	for (n = 0; n < nf; n++) {

	  phi0 = f[nf*index0 + n];
	  hydro_u(hydro, index0, u0);

	  /* west face (icm1 and ic) */

	  index1 = le_site_index(icm1, jc, kc);
	  hydro_u(hydro, index1, u1);
	  u = 0.5*(u0[X] + u1[X]);

	  if (u > 0.0) {
	    flux->fw[nf*index0 + n] = u*f[nf*index1 + n];
	  }
	  else {
	    flux->fw[nf*index0 + n] = u*phi0;
	  }

	  /* east face (ic and icp1) */

	  index1 = le_site_index(icp1, jc, kc);
	  hydro_u(hydro, index1, u1);
	  u = 0.5*(u0[X] + u1[X]);

	  if (u < 0.0) {
	    flux->fe[nf*index0 + n] = u*f[nf*index1 + n];
	  }
	  else {
	    flux->fe[nf*index0 + n] = u*phi0;
	  }

	  /* y direction */

	  index1 = le_site_index(ic, jc+1, kc);
	  hydro_u(hydro, index1, u1);
	  u = 0.5*(u0[Y] + u1[Y]);

	  if (u < 0.0) {
	    flux->fy[nf*index0 + n] = u*f[nf*index1 + n];
	  }
	  else {
	    flux->fy[nf*index0 + n] = u*phi0;
	  }

	  /* z direction */

	  index1 = le_site_index(ic, jc, kc+1);
	  hydro_u(hydro, index1, u1);
	  u = 0.5*(u0[Z] + u1[Z]);

	  if (u < 0.0) {
	    flux->fz[nf*index0 + n] = u*f[nf*index1 + n];
	  }
	  else {
	    flux->fz[nf*index0 + n] = u*phi0;
	  }
	}
	/* Next site */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  advection_le_2nd
 *
 *  'Centred difference' advective fluxes, allowing for LE planes.
 *
 *  Symmetric two-point stencil.
 *
 *****************************************************************************/

static int advection_le_2nd(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f) {
  int nlocal[3];
  int ic, jc, kc;
  int n;
  int index0, index1;
  int icp1, icm1;
  double u0[3], u1[3], u;

  assert(flux);
  assert(hydro);
  assert(f);

  coords_nlocal(nlocal);
  assert(coords_nhalo() >= 1);

  for (ic = 1; ic <= nlocal[X]; ic++) {
    icm1 = le_index_real_to_buffer(ic, -1);
    icp1 = le_index_real_to_buffer(ic, +1);
    for (jc = 0; jc <= nlocal[Y]; jc++) {
      for (kc = 0; kc <= nlocal[Z]; kc++) {

	index0 = le_site_index(ic, jc, kc);
	hydro_u(hydro, index0, u0);

	/* west face (icm1 and ic) */

	index1 = le_site_index(icm1, jc, kc);
	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[X] + u1[X]);

	for (n = 0; n < nf; n++) {
	  flux->fw[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}	

	/* east face (ic and icp1) */

	index1 = le_site_index(icp1, jc, kc);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[X] + u1[X]);

	for (n = 0; n < nf; n++) {
	  flux->fe[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* y direction */

	index1 = le_site_index(ic, jc+1, kc);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Y] + u1[Y]);

	for (n = 0; n < nf; n++) {
	  flux->fy[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* z direction */

	index1 = le_site_index(ic, jc, kc+1);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Z] + u1[Z]);

	for (n = 0; n < nf; n++) {
	  flux->fz[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* Next site */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  advection_le_3rd
 *
 *  Advective fluxes, allowing for LE planes.
 *
 *  In fact, formally second order wave-number extended scheme
 *  folowing Li, J. Comp. Phys. 113 235--255 (1997).
 *
 *  The stencil is three points, biased in upwind direction,
 *  with weights a1, a2, a3.
 *
 *****************************************************************************/

//TO DO - enable LE planes for targetDP

extern __targetConst__ int tc_nSites; 
extern __targetConst__ int tc_nhalo;
extern __targetConst__ int tc_Nall[3]; 

__targetEntry__ void advection_le_3rd_lattice(advflux_t * flux, 
					      hydro_t * hydro, int nf,
					      field_t * field) {
  int n;
  double u0[3], u1[3], u;
  int i;

  const double a1 = -0.213933;
  const double a2 =  0.927865;
  const double a3 =  0.286067;

  int index;  
__targetTLP__(index,tc_nSites){
    
    int coords[3];
    targetCoords3D(coords,tc_Nall,index);
    
    // if not a halo site:
    if (coords[0] >= tc_nhalo && 
	coords[1] >= 0 && 
	coords[2] >= 0 &&
	coords[0] < tc_Nall[X]-tc_nhalo &&  
	coords[1] < tc_Nall[Y]-tc_nhalo  &&  
	coords[2] < tc_Nall[Z]-tc_nhalo ){ 


      int index0, index1, index2;
      index0 = targetIndex3D(coords[0],coords[1],coords[2],tc_Nall);

      for(i=0;i<3;i++)
        u0[i]=hydro->u[HYADR(tc_nSites,3,index0,i)];

	/* west face (icm1 and ic) */

	index1 = targetIndex3D(coords[0]-1,coords[1],coords[2],tc_Nall);

	for(i=0;i<3;i++)
	  u1[i]=hydro->u[HYADR(tc_nSites,3,index1,i)];

	u = 0.5*(u0[X] + u1[X]);

	if (u > 0.0) {
	  index2 = targetIndex3D(coords[0]-2,coords[1],coords[2],tc_Nall);

	  for (n = 0; n < nf; n++) {
	    flux->fw[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index1 + n]
	       + a3*field->data[nf*index0 + n]);
	  }
	}
	else {

	index2 = targetIndex3D(coords[0]+1,coords[1],coords[2],tc_Nall);

	  for (n = 0; n < nf; n++) {
	    flux->fw[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index0 + n]
	       + a3*field->data[nf*index1 + n]);
	  }
	}

	/* east face (ic and icp1) */

	index1 = targetIndex3D(coords[0]+1,coords[1],coords[2],tc_Nall);

	for(i=0;i<3;i++)
	  u1[i]=hydro->u[HYADR(tc_nSites,3,index1,i)]
;
	u = 0.5*(u0[X] + u1[X]);

	if (u < 0.0) {
	index2 = targetIndex3D(coords[0]+2,coords[1],coords[2],tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fe[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index1 + n]
	       + a3*field->data[nf*index0 + n]);
	  }
	}
	else {
	index2 = targetIndex3D(coords[0]-1,coords[1],coords[2],tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fe[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index0 + n]
	       + a3*field->data[nf*index1 + n]);
	  }
	}

	/* y direction */

	index1 = targetIndex3D(coords[0],coords[1]+1,coords[2],tc_Nall);

	for(i=0;i<3;i++)
	  u1[i]=hydro->u[HYADR(tc_nSites,3,index1,i)];

	u = 0.5*(u0[Y] + u1[Y]);

	if (u < 0.0) {
	index2 = targetIndex3D(coords[0],coords[1]+2,coords[2],tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fy[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index1 + n]
	       + a3*field->data[nf*index0 + n]);
	  }
	}
	else {
	index2 = targetIndex3D(coords[0],coords[1]-1,coords[2],tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fy[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index0 + n]
	       + a3*field->data[nf*index1 + n]);
	  }
	}

	/* z direction */

	index1 = targetIndex3D(coords[0],coords[1],coords[2]+1,tc_Nall);

	for(i=0;i<3;i++)
	  u1[i]=hydro->u[HYADR(tc_nSites,3,index1,i)];

	u = 0.5*(u0[Z] + u1[Z]);

	if (u < 0.0) {
	index2 = targetIndex3D(coords[0],coords[1],coords[2]+2,tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fz[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index1 + n]
	       + a3*field->data[nf*index0 + n]);
	  }
	}
	else {
	index2 = targetIndex3D(coords[0],coords[1],coords[2]-1,tc_Nall);
	  for (n = 0; n < nf; n++) {
	    flux->fz[nf*index0 + n] =
	      u*(a1*field->data[nf*index2 + n]
	       + a2*field->data[nf*index0 + n]
	       + a3*field->data[nf*index1 + n]);
	  }
	}

	/* Next site */
      }
    }

  return;
}

static int advection_le_3rd(advflux_t * flux, hydro_t * hydro, int nf,
			    field_t * field) {
  int nlocal[3];

  assert(flux);
  assert(hydro);
  assert(field->data);

  coords_nlocal(nlocal);
  assert(coords_nhalo() >= 2);

  int nhalo;
  nhalo = coords_nhalo();


  int Nall[3];
  Nall[X]=nlocal[X]+2*nhalo;  Nall[Y]=nlocal[Y]+2*nhalo;  Nall[Z]=nlocal[Z]+2*nhalo;

  int nSites=Nall[X]*Nall[Y]*Nall[Z];

  // copy input data to target
  hydro_t* t_hydro = hydro->tcopy; //target copy of hydro structure
  field_t* t_field = field->tcopy; //target copy of field structure

  double* tmpptr;
  copyFromTarget(&tmpptr,&(t_hydro->u),sizeof(double*)); 
  copyToTarget(tmpptr,hydro->u,3*nSites*sizeof(double));

  copyFromTarget(&tmpptr,&(t_field->data),sizeof(double*)); 
  copyToTarget(tmpptr,field->data,nf*nSites*sizeof(double));


  //copy lattice shape constants to target ahead of execution
  copyConstToTarget(&tc_nSites,&nSites, sizeof(int));
  copyConstToTarget(&tc_nhalo,&nhalo, sizeof(int));
  copyConstToTarget(tc_Nall,Nall, 3*sizeof(int));
  

  //execute lattice-based operation on target
  advection_le_3rd_lattice __targetLaunch__(nSites) (flux->tcopy,hydro->tcopy,nf,field->tcopy);

  // copy output data from target

  advflux_t* t_flux = flux->tcopy; //target copy of flux structure

  copyFromTarget(&tmpptr,&(t_flux->fe),sizeof(double*)); 
  copyFromTarget(flux->fe,tmpptr,nf*nSites*sizeof(double));

  copyFromTarget(&tmpptr,&(t_flux->fw),sizeof(double*)); 
  copyFromTarget(flux->fw,tmpptr,nf*nSites*sizeof(double));

  copyFromTarget(&tmpptr,&(t_flux->fy),sizeof(double*)); 
  copyFromTarget(flux->fy,tmpptr,nf*nSites*sizeof(double));

  copyFromTarget(&tmpptr,&(t_flux->fz),sizeof(double*)); 
  copyFromTarget(flux->fz,tmpptr,nf*nSites*sizeof(double));


  return 0;
}

/****************************************************************************
 *
 *  advection_le_4th
 *
 *  Advective fluxes, allowing for LE planes.
 *
 *  The stencil is four points.
 *
 ****************************************************************************/

static int advection_le_4th(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f) {
  int nlocal[3];
  int ic, jc, kc;
  int n;
  int index0, index1;
  int icm2, icm1, icp1, icp2;
  double u0[3], u1[3], u;

  const double a1 = (1.0/16.0); /* Interpolation weight */
  const double a2 = (9.0/16.0); /* Interpolation weight */

  assert(flux);
  assert(hydro);
  assert(f);

  coords_nlocal(nlocal);
  assert(coords_nhalo() >= 2);

  for (ic = 1; ic <= nlocal[X]; ic++) {
    icm2 = le_index_real_to_buffer(ic, -2);
    icm1 = le_index_real_to_buffer(ic, -1);
    icp1 = le_index_real_to_buffer(ic, +1);
    icp2 = le_index_real_to_buffer(ic, +2);

    for (jc = 0; jc <= nlocal[Y]; jc++) {
      for (kc = 0; kc <= nlocal[Z]; kc++) {

	index0 = le_site_index(ic, jc, kc);
	hydro_u(hydro, index0, u0);

	/* west face (icm1 and ic) */

	index1 = le_site_index(icm1, jc, kc);
	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[X] + u1[X]);
	
	for (n = 0; n < nf; n++) {
	  flux->fw[nf*index0 + n] =
	    u*(- a1*f[nf*le_site_index(icm2, jc, kc) + n]
	       + a2*f[nf*index1 + n]
	       + a2*f[nf*index0 + n]
	       - a1*f[nf*le_site_index(icp1, jc, kc) + n]);
	}

	/* east face */

	index1 = le_site_index(icp1, jc, kc);
	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[X] + u1[X]);

	for (n = 0; n < nf; n++) {
	  flux->fe[nf*index0 + n] =
	    u*(- a1*f[nf*le_site_index(icm1, jc, kc) + n]
	       + a2*f[nf*index0 + n]
	       + a2*f[nf*index1 + n]
	       - a1*f[nf*le_site_index(icp2, jc, kc) + n]);
	}

	/* y-direction */

	index1 = le_site_index(ic, jc+1, kc);
	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Y] + u1[Y]);

	for (n = 0; n < nf; n++) {
	  flux->fy[nf*index0 + n] =
	    u*(- a1*f[nf*le_site_index(ic, jc-1, kc) + n]
	       + a2*f[nf*index0 + n]
	       + a2*f[nf*index1 + n]
	       - a1*f[nf*le_site_index(ic, jc+2, kc) + n]);
	}

	/* z-direction */

	index1 = le_site_index(ic, jc, kc+1);
	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Z] + u1[Z]);

	for (n = 0; n < nf; n++) {
	  flux->fz[nf*index0 + n] =
	    u*(- a1*f[nf*le_site_index(ic, jc, kc-1) + n]
	       + a2*f[nf*index0 + n]
	       + a2*f[nf*index1 + n]
	       - a1*f[nf*le_site_index(ic, jc, kc+2) + n]);
	}

	/* Next interface. */
      }
    }
  }

  return 0;
}

/****************************************************************************
 *
 *  advection_le_5th
 *
 *  Advective fluxes, allowing for LE planes.
 *
 *  Formally fourth-order accurate wavenumber-extended scheme of
 *  Li, J. Comp. Phys. 133 235-255 (1997).
 *
 *  The stencil is five points, biased in the upwind direction,
 *  with weights a1--a5.
 *
 ****************************************************************************/

static int advection_le_5th(advflux_t * flux, hydro_t * hydro, int nf,
			    double * f) {
  int nlocal[3];
  int ic, jc, kc;
  int n;
  int index0, index1;
  int icm2, icm1, icp1, icp2, icm3, icp3;
  double u0[3], u1[3], u;

  const double a1 =  0.055453;
  const double a2 = -0.305147;
  const double a3 =  0.916054;
  const double a4 =  0.361520;
  const double a5 = -0.027880;

  assert(flux);
  assert(hydro);
  assert(f);

  coords_nlocal(nlocal);

  for (ic = 1; ic <= nlocal[X]; ic++) {
    icm3 = le_index_real_to_buffer(ic, -3);
    icm2 = le_index_real_to_buffer(ic, -2);
    icm1 = le_index_real_to_buffer(ic, -1);
    icp1 = le_index_real_to_buffer(ic, +1);
    icp2 = le_index_real_to_buffer(ic, +2);
    icp3 = le_index_real_to_buffer(ic, +3);
    for (jc = 0; jc <= nlocal[Y]; jc++) {
      for (kc = 0; kc <= nlocal[Z]; kc++) {

        index0 = le_site_index(ic, jc, kc);
        hydro_u(hydro, index0, u0);

        /* west face (icm1 and ic) */

        index1 = le_site_index(icm1, jc, kc);
        hydro_u(hydro, index1, u1);
        u = 0.5*(u0[X] + u1[X]);

        if (u > 0.0) {
          for (n = 0; n < nf; n++) {
            flux->fw[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(icm3, jc, kc) + n]
	       + a2*f[nf*le_site_index(icm2, jc, kc) + n]
               + a3*f[nf*index1 + n]
               + a4*f[nf*index0 + n]
	       + a5*f[nf*le_site_index(icp1, jc, kc) + n]);
          }
        }
        else {
          for (n = 0; n < nf; n++) {
            flux->fw[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(icp2, jc, kc) + n]
	       + a2*f[nf*le_site_index(icp1, jc, kc) + n]
               + a3*f[nf*index0 + n]
               + a4*f[nf*index1 + n]
	       + a5*f[nf*le_site_index(icm2, jc, kc) + n]);
          }
	}

        /* east face */

        index1 = le_site_index(icp1, jc, kc);
        hydro_u(hydro, index1, u1);
        u = 0.5*(u0[X] + u1[X]);

        if (u < 0.0) {
          for (n = 0; n < nf; n++) {
            flux->fe[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(icp3, jc, kc) + n]
	       + a2*f[nf*le_site_index(icp2, jc, kc) + n]
               + a3*f[nf*index1 + n]
               + a4*f[nf*index0 + n]
	       + a5*f[nf*le_site_index(icm1, jc, kc) + n]);
          }
        }
        else {
          for (n = 0; n < nf; n++) {
            flux->fe[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(icm2, jc, kc) + n]
	       + a2*f[nf*le_site_index(icm1, jc, kc) + n]
               + a3*f[nf*index0 + n]
               + a4*f[nf*index1 + n]
	       + a5*f[nf*le_site_index(icp2, jc, kc) + n]);
          }
        }

        /* y-direction */

        index1 = le_site_index(ic, jc+1, kc);
        hydro_u(hydro, index1, u1);
        u = 0.5*(u0[Y] + u1[Y]);

        if (u < 0.0) {
          for (n = 0; n < nf; n++) {
            flux->fy[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(ic, jc+3, kc) + n]
	       + a2*f[nf*le_site_index(ic, jc+2, kc) + n]
               + a3*f[nf*index1 + n]
               + a4*f[nf*index0 + n]
	       + a5*f[nf*le_site_index(ic, jc-1, kc) + n]);
          }
        }
        else {
          for (n = 0; n < nf; n++) {
            flux->fy[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(ic, jc-2, kc) + n]
	       + a2*f[nf*le_site_index(ic, jc-1, kc) + n]
               + a3*f[nf*index0 + n]
               + a4*f[nf*index1 + n]
	       + a5*f[nf*le_site_index(ic, jc+2, kc) + n]);
          }
        }

        /* z-direction */

        index1 = le_site_index(ic, jc, kc+1);
        hydro_u(hydro, index1, u1);
        u = 0.5*(u0[Z] + u1[Z]);

        if (u < 0.0) {
          for (n = 0; n < nf; n++) {
            flux->fz[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(ic, jc, kc+3) + n]
	       + a2*f[nf*le_site_index(ic, jc, kc+2) + n]
               + a3*f[nf*index1 + n]
               + a4*f[nf*index0 + n]
	       + a5*f[nf*le_site_index(ic, jc, kc-1) + n]);
          }
        }
        else {
          for (n = 0; n < nf; n++) {
            flux->fz[nf*index0 + n] =
              u*(a1*f[nf*le_site_index(ic, jc, kc-2) + n]
	       + a2*f[nf*le_site_index(ic, jc, kc-1) + n]
               + a3*f[nf*index0 + n]
               + a4*f[nf*index1 + n]
	       + a5*f[nf*le_site_index(ic, jc, kc+2) + n]);
          }
        }

        /* Next interface. */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  advective_fluxes
 *
 *  General routine for nf fields at starting address f.
 *  No Lees Edwards boundaries.
 *
 *  The storage of the field(s) for all the related routines is
 *  assumed to be f[index][nf], where index is the spatial index.
 *
 *****************************************************************************/

int advective_fluxes(hydro_t * hydro, int nf, double * f, double * fe,
		     double * fy, double * fz) {

  assert(hydro);
  assert(nf > 0);
  assert(f);
  assert(fe);
  assert(fy);
  assert(fz);
  assert(le_get_nplane_total() == 0);

  advective_fluxes_2nd(hydro, nf, f, fe, fy, fz);

  return 0;
}

/*****************************************************************************
 *
 *  advective_fluxes_2nd
 *
 *  'Centred difference' advective fluxes. No LE planes.
 *
 *  Symmetric two-point stencil.
 *
 *****************************************************************************/

int advective_fluxes_2nd(hydro_t * hydro, int nf, double * f, double * fe,
			 double * fy, double * fz) {
  int nlocal[3];
  int ic, jc, kc;
  int n;
  int index0, index1;
  double u0[3], u1[3], u;

  assert(hydro);
  assert(nf > 0);
  assert(f);
  assert(fe);
  assert(fy);
  assert(fz);

  coords_nlocal(nlocal);
  assert(coords_nhalo() >= 1);
  assert(le_get_nplane_total() == 0);

  for (ic = 0; ic <= nlocal[X]; ic++) {
    for (jc = 0; jc <= nlocal[Y]; jc++) {
      for (kc = 0; kc <= nlocal[Z]; kc++) {

	index0 = coords_index(ic, jc, kc);
	hydro_u(hydro, index0, u0);

	/* east face (ic and icp1) */

	index1 = coords_index(ic+1, jc, kc);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[X] + u1[X]);

	for (n = 0; n < nf; n++) {
	  fe[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* y direction */

	index1 = coords_index(ic, jc+1, kc);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Y] + u1[Y]);

	for (n = 0; n < nf; n++) {
	  fy[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* z direction */

	index1 = coords_index(ic, jc, kc+1);

	hydro_u(hydro, index1, u1);
	u = 0.5*(u0[Z] + u1[Z]);

	for (n = 0; n < nf; n++) {
	  fz[nf*index0 + n] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	}

	/* Next site */
      }
    }
  }

  return 0;
}

/*****************************************************************************
 *
 *  advective_fluxes_d3qx
 *
 *  General routine for nf fields at starting address f.
 *  No Lees Edwards boundaries.
 *
 *  The storage of the field(s) for all the related routines is
 *  assumed to be f[index][nf], where index is the spatial index.
 *
 *****************************************************************************/

int advective_fluxes_d3qx(hydro_t * hydro, int nf, double * f, 
					double ** flx) {

  assert(hydro);
  assert(nf > 0);
  assert(f);
  assert(flx);
  assert(le_get_nplane_total() == 0);

  advective_fluxes_2nd_d3qx(hydro, nf, f, flx);

  return 0;
}

/*****************************************************************************
 *
 *  advective_fluxes_2nd_d3qx
 *
 *  'Centred difference' advective fluxes. No LE planes.
 *
 *  Symmetric two-point stencil.
 *
 *****************************************************************************/

int advective_fluxes_2nd_d3qx(hydro_t * hydro, int nf, double * f, 
					double ** flx) {

  int nlocal[3];
  int ic, jc, kc, c;
  int n;
  int index0, index1;
  double u0[3], u1[3], u;

  assert(hydro);
  assert(nf > 0);
  assert(f);
  assert(flx);
  assert(le_get_nplane_total() == 0);

  coords_nlocal(nlocal);
  assert(coords_nhalo() >= 1);

  for (ic = 1; ic <= nlocal[X]; ic++) {
    for (jc = 1; jc <= nlocal[Y]; jc++) {
      for (kc = 1; kc <= nlocal[Z]; kc++) {

	index0 = coords_index(ic, jc, kc);
	hydro_u(hydro, index0, u0);

        for (c = 1; c < PSI_NGRAD; c++) {

	  index1 = coords_index(ic + psi_gr_cv[c][X], jc + psi_gr_cv[c][Y], kc + psi_gr_cv[c][Z]);
	  hydro_u(hydro, index1, u1);

	  u = 0.5*((u0[X] + u1[X])*psi_gr_cv[c][X] + (u0[Y] + u1[Y])*psi_gr_cv[c][Y] + (u0[Z] + u1[Z])*psi_gr_cv[c][Z]);

	  for (n = 0; n < nf; n++) {
	    flx[nf*index0 + n][c - 1] = u*0.5*(f[nf*index1 + n] + f[nf*index0 + n]);
	  }

	}

	/* Next site */
      }
    }
  }

  return 0;
}
