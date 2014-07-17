/*****************************************************************************
 *
 *  test_pair_lj_cut.c
 *
 *  Edinburgh Soft Matter and Statistical Physics Group and
 *  Edinburgh Parallel Computing Centre
 *
 *  (c) The University of Edinburgh (2014)
 *  Contributing authors;
 *    Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *****************************************************************************/

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

#include "pe.h"
#include "coords.h"
#include "colloids_halo.h"
#include "pair_lj_cut.h"

#define PAIR_EPSILON 1.0
#define PAIR_SIGMA   1.0
#define PAIR_RC      3.0

int test_pair_lj_cut_suite(void);
int test_pair_lj_cut1(void);
int test_pair_lj_cut2(void);
int test_pair_config1(colloids_info_t * cinfo, interact_t * interact,
		      pair_lj_cut_t * lj);

/*****************************************************************************
 *
 *  main
 *
 *****************************************************************************/

int main(int argc, char ** argv) {


  MPI_Init(&argc, &argv);

  test_pair_lj_cut_suite();

  MPI_Finalize();

  return 0;
}

/*****************************************************************************
 *
 *  test_pair_lj_cut_suite
 *
 *****************************************************************************/

int test_pair_lj_cut_suite(void) {

  pe_init();
  coords_init();

  test_pair_lj_cut1();
  test_pair_lj_cut2();

  coords_finish();
  pe_finalise();

  return 0;
}

/*****************************************************************************
 *
 *  test_pair_lj_cut1
 *
 *****************************************************************************/

int test_pair_lj_cut1(void) {

  pair_lj_cut_t * lj = NULL;
  double h, f, v;

  pair_lj_cut_create(&lj);
  assert(lj);

  pair_lj_cut_param_set(lj, PAIR_EPSILON, PAIR_SIGMA, PAIR_RC);

  h = PAIR_SIGMA;
  pair_lj_cut_single(lj, h, &f, &v);
  assert(fabs(f - 24.01094383) < FLT_EPSILON);
  assert(fabs(v -  0.027367102) < FLT_EPSILON);

  h = pow(2.0, 1.0/6.0)*PAIR_SIGMA;
  pair_lj_cut_single(lj, h, &f, &v);
  assert(fabs(f - 0.010943830) < FLT_EPSILON);
  assert(fabs(v - -0.97397310) < FLT_EPSILON);

  h = PAIR_RC;
  pair_lj_cut_single(lj, h, &f, &v);
  assert(fabs(f - 0.0) < FLT_EPSILON);
  assert(fabs(v - 0.0) < FLT_EPSILON);

  pair_lj_cut_free(lj);

  return 0;
}

/*****************************************************************************
 *
 *  test_pair_ss_cut2
 *
 *****************************************************************************/

int test_pair_lj_cut2(void) {

  int ncell[3] = {2, 2, 2};

  colloids_info_t * cinfo = NULL;
  interact_t * interact = NULL;
  pair_lj_cut_t * lj = NULL;

  colloids_info_create(ncell, &cinfo);
  interact_create(&interact);
  pair_lj_cut_create(&lj);

  assert(cinfo);
  assert(interact);
  assert(lj);

  pair_lj_cut_param_set(lj, PAIR_EPSILON, PAIR_SIGMA, PAIR_RC);
  pair_lj_cut_register(lj, interact);

  test_pair_config1(cinfo, interact, lj);

  /* Finish */

  pair_lj_cut_free(lj);
  interact_free(interact);
  colloids_info_free(cinfo);

  return 0;
}

/*****************************************************************************
 *
 *  test_pair_config1
 *
 *****************************************************************************/

int test_pair_config1(colloids_info_t * cinfo, interact_t * interact,
		      pair_lj_cut_t * lj) {

  int nc;
  double a0 = 1.25;
  double ah = 1.25;
  double dh = 0.1;
  double h, f, v;
  double r1[3];
  double r2[3];
  double stats[INTERACT_STAT_MAX];
  double stats_local[INTERACT_STAT_MAX];

  colloid_t * pc1 = NULL;
  colloid_t * pc2 = NULL;

  assert(cinfo);
  assert(interact);
  assert(lj);

  h = 2.0*ah + dh;
  r1[X] = 0.5*L(X) - 0.5*h;
  r1[Y] = 0.5*L(Y);
  r1[Z] = 0.5*L(Z);

  colloids_info_add_local(cinfo, 1, r1, &pc1);
  if (pc1) {
    pc1->s.a0 = a0;
    pc1->s.ah = ah;
  }

  r2[X] = r1[X] + h;
  r2[Y] = r1[Y];
  r2[Z] = r1[Z];

  colloids_info_add_local(cinfo, 2, r2, &pc2);
  if (pc2) {
    pc2->s.a0 = a0;
    pc2->s.ah = ah;
  }

  colloids_info_ntotal_set(cinfo);
  colloids_info_ntotal(cinfo, &nc);
  assert(nc == 2);

  colloids_halo_state(cinfo);

  /* Check interaction against single version */

  interact_pairwise(interact, cinfo);
  pair_lj_cut_single(lj, h, &f, &v);

  if (pe_size() == 1) {
    assert(fabs(pc1->force[X] - 0.018743896) < FLT_EPSILON);
    assert(fabs(pc1->force[Y] - 0.0)         < FLT_EPSILON);
    assert(fabs(pc1->force[Z] - 0.0)         < FLT_EPSILON);

    assert(fabs(pc2->force[X] + 0.018743896) < FLT_EPSILON);
    assert(fabs(pc2->force[Y] + 0.0)         < FLT_EPSILON);
    assert(fabs(pc2->force[Z] + 0.0)         < FLT_EPSILON);
  }

  pair_lj_cut_stats(lj, stats_local);

  MPI_Allreduce(stats_local, stats, INTERACT_STAT_MAX, MPI_DOUBLE, MPI_SUM,
		cart_comm());

  assert(fabs(stats[INTERACT_STAT_VLOCAL] - v) < FLT_EPSILON);

  MPI_Allreduce(stats_local, stats, INTERACT_STAT_MAX, MPI_DOUBLE, MPI_MIN,
		cart_comm());

  assert(fabs(stats[INTERACT_STAT_RMINLOCAL] - h) < FLT_EPSILON);
  assert(fabs(stats[INTERACT_STAT_HMINLOCAL] - dh) < FLT_EPSILON);

  return 0;
}