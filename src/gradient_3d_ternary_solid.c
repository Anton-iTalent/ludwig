/*****************************************************************************
 *
 *  gradient_3d_ternary_solid.c
 *
 *  Gradient routines for three phase model of Semprebon where wetting
 *  parameters are set via the map structure.
 *
 *  This is the 'predictor corrector' method described by Desplat et al.
 *  Comp. Phys. Comm. 134, 273--290 (2000).
 *
 *  Wetting parameters per site must be available from the map structure;
 *  parameters h_1 and h_2 are expected.
 *
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  (c) 2019-2021 The University of Edinburgh
 *
 *  Contributing authors:
 *  Shan Chen (shan.chen@epfl.ch)
 *  Sergi Granados Leyva (sgranale7@alumnes.ub.edu)
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *****************************************************************************/

#include <assert.h>

#include "pe.h"
#include "coords.h"
#include "kernel.h"
#include "field_s.h"
#include "field_grad_s.h"
#include "gradient_3d_ternary_solid.h"

struct solid_s {
  map_t * map;
  fe_ternary_t * fe_ternary;
};

static struct solid_s static_solid = {0};

/* These are the 'links' used to form the gradients at boundaries. */


#define NGRAD_ 27
static __constant__ int bs_cv[NGRAD_][3] = {{ 0, 0, 0},
				 {-1,-1,-1}, {-1,-1, 0}, {-1,-1, 1},
                                 {-1, 0,-1}, {-1, 0, 0}, {-1, 0, 1},
                                 {-1, 1,-1}, {-1, 1, 0}, {-1, 1, 1},
                                 { 0,-1,-1}, { 0,-1, 0}, { 0,-1, 1},
                                 { 0, 0,-1},             { 0, 0, 1},
				 { 0, 1,-1}, { 0, 1, 0}, { 0, 1, 1},
				 { 1,-1,-1}, { 1,-1, 0}, { 1,-1, 1},
				 { 1, 0,-1}, { 1, 0, 0}, { 1, 0, 1},
				 { 1, 1,-1}, { 1, 1, 0}, { 1, 1, 1}};


__global__ void grad_ternary_solid_kernel(kernel_ctxt_t * ktx,
					  field_grad_t * fg, int nf,
					  map_t * map,
					  double rkappa1, double rkappa2,
					  double alpha);

/*****************************************************************************
 *
 *  grad_3d_ternary_solid_map_set
 *
 *****************************************************************************/

__host__ int grad_3d_ternary_solid_map_set(map_t * map) {

  int ndata;
  assert(map);

  static_solid.map = map;

  /* We expect exactly two wetting parameters h_1, h_2 */

  map_ndata(map, &ndata);

  if (ndata != 2) pe_fatal(map->pe, "Check wetting parameters %d\n", ndata);

  return 0;
}

/*****************************************************************************
 *
 *  grad_3d_ternary_solid_fe_set
 *
 *****************************************************************************/

__host__ int grad_3d_ternary_solid_fe_set(fe_ternary_t * fe) {

  assert(fe);
  
  static_solid.fe_ternary = fe;
 
  return 0;
}

/*****************************************************************************
 *
 *  grad_3d_ternary_solid_d2
 *
 *****************************************************************************/

__host__ int grad_3d_ternary_solid_d2(field_grad_t * fgrad) {

  int nextra;
  int nlocal[3];
  double rkappa1,rkappa2,alpha;
  dim3 nblk, ntpb;
  kernel_info_t limits;
  kernel_ctxt_t * ctxt = NULL;
  fe_ternary_param_t param;

  cs_nhalo(fgrad->field->cs, &nextra);
  nextra -= 1;
  cs_nlocal(fgrad->field->cs, nlocal);

  assert(nextra >= 0);
  assert(static_solid.map);
  assert(static_solid.fe_ternary);

  fe_ternary_param(static_solid.fe_ternary, &param);
   
  rkappa1 = 1.0/param.kappa1;
  rkappa2 = 1.0/param.kappa2;
  alpha = param.alpha;
 

  limits.imin = 1 - nextra; limits.imax = nlocal[X] + nextra;
  limits.jmin = 1 - nextra; limits.jmax = nlocal[Y] + nextra;
  limits.kmin = 1 - nextra; limits.kmax = nlocal[Z] + nextra;

  kernel_ctxt_create(fgrad->field->cs, NSIMDVL, limits, &ctxt);
  kernel_ctxt_launch_param(ctxt, &nblk, &ntpb);

  tdpLaunchKernel(grad_ternary_solid_kernel, nblk, ntpb, 0, 0,
		  ctxt->target, fgrad->target, fgrad->field->nf,
		  static_solid.map->target, rkappa1, rkappa2, alpha);
    
  tdpDeviceSynchronize();

  kernel_ctxt_free(ctxt);

  return 0;
}

/****************************************************************************
 *
 *  grad_ternary_solid_kernel
 *
 *  kappa is the interfacial energy penalty in the symmetric picture.
 *  rkappa is (1.0/kappa). 
 *
 ****************************************************************************/

__global__ void grad_ternary_solid_kernel(kernel_ctxt_t * ktx,
					  field_grad_t * fg, int nf,
					  map_t * map,
					  double rkappa1, double rkappa2,
					  double alpha) {
  int kindex;
  int kiterations;
  const double r9 = (1.0/9.0);     /* normaliser for grad */
  const double r18 = (1.0/18.0);   /* normaliser for delsq */


  kiterations = kernel_iterations(ktx);

  for_simt_parallel(kindex, kiterations, 1) {

    int nop;
    int ic, jc, kc, ic1, jc1, kc1;
    int ia, index, p;
    int n;

    int isite[NGRAD_];

    double count[NGRAD_];
    double gradt[NGRAD_];
    double gradn[3];
    double dphi;
    double h1,h2;

    int status;
    double wet[2] = {0.0, 0.0};

    field_t * phi = NULL;

    nop = fg->field->nf;
    phi = fg->field;

    ic = kernel_coords_ic(ktx, kindex);
    jc = kernel_coords_jc(ktx, kindex);
    kc = kernel_coords_kc(ktx, kindex);

    index = kernel_coords_index(ktx, ic, jc, kc);
    map_status(map, index, &status);

    if (status == MAP_FLUID) {

      /* Set solid/fluid flag to index neighbours */

      for (p = 1; p < NGRAD_; p++) {
	ic1 = ic + bs_cv[p][X];
	jc1 = jc + bs_cv[p][Y];
	kc1 = kc + bs_cv[p][Z];

	isite[p] = kernel_coords_index(ktx, ic1, jc1, kc1);
	map_status(map, isite[p], &status);
	if (status != MAP_FLUID) isite[p] = -1;
      }

      for (n = 0; n < nop; n++) {

	for (ia = 0; ia < 3; ia++) {
	  count[ia] = 0.0;
	  gradn[ia] = 0.0;
	}
	  
	for (p = 1; p < NGRAD_; p++) {

	  if (isite[p] == -1) continue;

	  dphi
	    = phi->data[addr_rank1(phi->nsites, nop, isite[p], n)]
	    - phi->data[addr_rank1(phi->nsites, nop, index,    n)];
	  gradt[p] = dphi;

	  for (ia = 0; ia < 3; ia++) {
	    gradn[ia] += bs_cv[p][ia]*dphi;
	    count[ia] += bs_cv[p][ia]*bs_cv[p][ia];
	  }
	}

	for (ia = 0; ia < 3; ia++) {
	  if (count[ia] > 0.0) gradn[ia] /= count[ia];
	}

	/* Estimate gradient at boundaries */

	for (p = 1; p < NGRAD_; p++) {

	  if (isite[p] == -1) {

	    /* Set gradient phi at boundary following wetting properties */

	    ia = kernel_coords_index(ktx, ic + bs_cv[p][X], jc + bs_cv[p][Y],
				     kc + bs_cv[p][Z]);
	    map_data(map, ia, wet);
          
	    /* At the time use status_with_c_h and set h as h1, c as h2 through
	       capillary.c file */
	    h1 = wet[1];
	    h2 = wet[0];
         
	    if (n == 0) {
              gradt[p] = (-h1*rkappa1 + h2*rkappa2) /(alpha*alpha);
	    }
	    else {
              gradt[p] = (h1*rkappa1 + h2*rkappa2) /(alpha*alpha);
	    }
          
	  }
	}
 
	/* Accumulate the final gradients */

	dphi = 0.0;
	for (ia = 0; ia < 3; ia++) {
	  gradn[ia] = 0.0;
	}

	for (p = 1; p < NGRAD_; p++) {
	  dphi += gradt[p];
	  for (ia = 0; ia < 3; ia++) {
	    gradn[ia] += gradt[p]*bs_cv[p][ia];
	  }
	}

	fg->delsq[addr_rank1(phi->nsites, nop, index, n)] = r9*dphi;
	for (ia = 0; ia < 3; ia++) {
	  fg->grad[addr_rank2(phi->nsites,nop,3,index,n,ia)] = r18*gradn[ia];
	}
      }

      /* Next fluid site */
    }
  }

  return;
}
