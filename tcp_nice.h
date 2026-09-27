/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TCP nice congestion control interface
 */
 #ifndef __TCP_nice_H
 #define __TCP_nice_H 1
 
 /* nice variables */
 struct nice {
	 u32	beg_snd_nxt;	/* right edge during last RTT */
	 u32	beg_snd_una;	/* left edge  during last RTT */
	 u32	beg_snd_cwnd;	/* saves the size of the cwnd */
	 u8		doing_nice_now;/* if true, do nice for this RTT */
	 u16	cntRTT;		/* # of RTTs measured within last RTT */
	 u32	minRTT;		/* min of RTTs measured within last RTT (in usec) */	
	 u32	baseRTT;	/* the min of all nice RTT measurements seen (in usec) */
	 /* added for Nice implementation */
	 u32 	maxRTT;
	 u32 	thresholdRTT;
	 u32 	numCong; 	
 };
 
 void tcp_nice_init(struct sock *sk);
 void tcp_nice_state(struct sock *sk, u8 ca_state);
 void tcp_nice_pkts_acked(struct sock *sk, const struct ack_sample *sample);
 void tcp_nice_cwnd_event(struct sock *sk, enum tcp_ca_event event);
 size_t tcp_nice_get_info(struct sock *sk, u32 ext, int *attr,
			   union tcp_cc_info *info);
 
 #endif	/* __TCP_nice_H */
 