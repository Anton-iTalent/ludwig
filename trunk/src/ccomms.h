/*****************************************************************************
 *
 *  ccomms.h
 *
 *  Colloid halo communications.
 *
 *  Kevin Stratford (kevin@epcc.ed.ac.uk)
 *
 *****************************************************************************/

#ifndef _CCOMMS_H
#define _CCOMMS_H


enum message_type {CHALO_TYPE1 = 0, CHALO_TYPE2 = 1, CHALO_TYPE6 = 2};

void CCOM_init_halos(void);
void CCOM_halo_particles(void);
void CCOM_halo_sum(const int);
void CMPI_init_messages(void);

#endif
