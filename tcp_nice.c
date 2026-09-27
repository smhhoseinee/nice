// SPDX-License-Identifier: GPL-2.0-only
/*
 * TCP Nice congestion control, as a Linux kernel module.
 *
 * Author: Seyed Mohammad Hossein (Saam) Hosseini, University of Colorado Boulder.
 * Written for CSCI 7000 Advanced Network Protocols, Spring 2025.
 *
 * Algorithm from:
 *   Arun Venkataramani, Ravi Kokku, and Mike Dahlin.
 *   "TCP Nice: A Mechanism for Background Transfers." OSDI 2002.
 *
 * The code starts from the Linux TCP Vegas module (net/ipv4/tcp_vegas.c,
 * by Stephen Hemminger and others) and adds Nice's congestion detector:
 *   1. A packet counts as delayed when its RTT exceeds
 *      baseRTT + THRESHOLD% of (maxRTT - baseRTT).
 *   2. When more than FRACTION% of a window's packets are delayed, the
 *      congestion window is halved (multiplicative decrease).
 * Unlike the paper, the window never drops below 2 packets: Linux
 * requires cwnd >= 2, so Nice's fractional windows are not implemented.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inet_diag.h>
#include <net/tcp.h>

#include "tcp_nice.h"

/* Module parameters */
static int alpha = 2;
static int beta = 4;
static int gamma = 1;
/* Constants for Nice congestion detection */
#define THRESHOLD 10
#define FRACTION 20
#define SCALE 100

module_param(alpha, int, 0644);
MODULE_PARM_DESC(alpha, "lower bound of packets in network (Vegas parameter)");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "upper bound of packets in network (Vegas parameter)");
module_param(gamma, int, 0644);
MODULE_PARM_DESC(gamma, "limit on increase (scale by 2) (Vegas parameter)");

static void nice_enable(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct nice *nice = inet_csk_ca(sk);

	/* Begin taking Nice samples next time we send something. */
	nice->doing_nice_now = 1;

	/* Set the beginning of the next send window. */
	nice->beg_snd_nxt = tp->snd_nxt;

	nice->cntRTT = 0;
	nice->minRTT = 0x7fffffff;
	/* Added for Nice implementation */
	nice->maxRTT = 0;
	nice->numCong = 0;
	nice->thresholdRTT = 0;
}

/* Stop taking Nice samples for now. */
static inline void nice_disable(struct sock *sk)
{
	struct nice *nice = inet_csk_ca(sk);

	nice->doing_nice_now = 0;
}

void tcp_nice_init(struct sock *sk)
{
	struct nice *nice = inet_csk_ca(sk);

	nice->baseRTT = 0x7fffffff;
	nice_enable(sk);
}
EXPORT_SYMBOL_GPL(tcp_nice_init);

/* Do RTT sampling needed for Nice.
 * We track:
 *   - baseRTT: minimum RTT seen (estimate of propagation delay)
 *   - minRTT: minimum RTT in current window
 *   - maxRTT: maximum RTT seen (estimate of RTT when queue is full)
 * 
 * We also count how many packets exceed the Nice threshold for
 * congestion detection.
 */
void tcp_nice_pkts_acked(struct sock *sk, const struct ack_sample *sample)
{
	struct nice *nice = inet_csk_ca(sk);
	u32 vrtt;

	if (sample->rtt_us < 0)
		return;

	/* Never allow zero rtt or baseRTT */
	vrtt = sample->rtt_us + 1;

	/* Filter to find propagation delay: */
	if (vrtt < nice->baseRTT)
		nice->baseRTT = vrtt;

	/* Find the min RTT during the last RTT to find
	 * the current prop. delay + queuing delay:
	 */
	nice->minRTT = min(nice->minRTT, vrtt);
	nice->cntRTT++;

	/* Update maxRTT (added for Nice implementation) */
	nice->maxRTT = max(nice->maxRTT, vrtt);
	
	/* Calculate threshold for Nice congestion detection:
	 * threshold = (SCALE-THRESHOLD)*baseRTT + THRESHOLD*maxRTT / SCALE
	 */
	nice->thresholdRTT = ((SCALE - THRESHOLD) * nice->baseRTT + THRESHOLD * nice->maxRTT) / SCALE;
	
	/* Count packets that exceed the threshold */
	if (vrtt > nice->thresholdRTT) {
		nice->numCong++;
		// Uncomment for debugging: printk(KERN_INFO "[HighDelay] curRTT=%u, thresholdRTT=%u, numCong=%u", vrtt, nice->thresholdRTT, nice->numCong);
	}
}
EXPORT_SYMBOL_GPL(tcp_nice_pkts_acked);

void tcp_nice_state(struct sock *sk, u8 ca_state)
{
	if (ca_state == TCP_CA_Open)
		nice_enable(sk);
	else
		nice_disable(sk);
}
EXPORT_SYMBOL_GPL(tcp_nice_state);

void tcp_nice_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START)
		tcp_nice_init(sk);
}
EXPORT_SYMBOL_GPL(tcp_nice_cwnd_event);

static inline u32 tcp_nice_ssthresh(struct tcp_sock *tp)
{
	return min(tp->snd_ssthresh, tp->snd_cwnd);
}

static void tcp_nice_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct nice *nice = inet_csk_ca(sk);

	if (!nice->doing_nice_now) {
		tcp_reno_cong_avoid(sk, ack, acked);
		return;
	}

	if (after(ack, nice->beg_snd_nxt)) {
		/* Do the Nice once-per-RTT cwnd adjustment. */

		/* Save the extent of the current window so we can use this
		 * at the end of the next RTT.
		 */
		nice->beg_snd_nxt = tp->snd_nxt;

		/* We do the Nice calculations only if we got enough RTT
		 * samples that we can be reasonably sure that we got
		 * at least one RTT sample that wasn't from a delayed ACK.
		 */
		if (nice->cntRTT <= 2) {
			/* We don't have enough RTT samples to do the Nice
			 * calculation, so we'll behave like Reno.
			 */
			tcp_reno_cong_avoid(sk, ack, acked);
		} else {
			u32 rtt, diff;
			u64 target_cwnd;
			
			/* Pluck out the RTT we are using for the Vegas calculations. */
			rtt = nice->minRTT;

			/* Calculate the cwnd we should have, if we weren't going too fast. */
			target_cwnd = (u64)tp->snd_cwnd * nice->baseRTT;
			do_div(target_cwnd, rtt);

			/* Calculate the difference between the window we had,
			 * and the window we would like to have.
			 */
			diff = tp->snd_cwnd * (rtt-nice->baseRTT) / nice->baseRTT;

			/* Check for Nice condition first */
			if (diff > gamma && tcp_in_slow_start(tp)) {
				/* Going too fast. Time to slow down
				 * and switch to congestion avoidance.
				 */
				tp->snd_cwnd = min(tp->snd_cwnd, (u32)target_cwnd + 1);
				tp->snd_ssthresh = tcp_nice_ssthresh(tp);
			} else if (tcp_in_slow_start(tp)) {
				/* Slow start. */
				tcp_slow_start(tp, acked);
			}
			/* This is the key Nice modification - check if congestion is detected */
			else if (nice->numCong > tp->snd_cwnd / (SCALE/FRACTION)) {
				// printk(KERN_INFO "[NICE] numCong=%u, cwnd=%u", nice->numCong, tp->snd_cwnd);
				/* Nice congestion detected - use multiplicative decrease */
				tp->snd_cwnd = tp->snd_cwnd / 2;
				// printk(KERN_INFO "[HALF] new_cwnd=%u", tp->snd_cwnd);
			} else {
				/* No Nice congestion, use Vegas congestion avoidance */
				if (diff > beta) {
					/* The old window was too fast, so we slow down. */
					tp->snd_cwnd--;
					tp->snd_ssthresh = tcp_nice_ssthresh(tp);
				} else if (diff < alpha) {
					/* We don't have enough extra packets in the network, so speed up. */
					tp->snd_cwnd++;
				}
				/* Else we're sending just the right amount */
			}

			/* Ensure cwnd stays within bounds */
			if (tp->snd_cwnd < 2)
				tp->snd_cwnd = 2;
			else if (tp->snd_cwnd > tp->snd_cwnd_clamp)
				tp->snd_cwnd = tp->snd_cwnd_clamp;

			tp->snd_ssthresh = tcp_current_ssthresh(sk);
		}

		/* Wipe the slate clean for the next RTT. */
		nice->cntRTT = 0;
		nice->minRTT = 0x7fffffff;
		/* Clear maxRTT, numCong, and thresholdRTT for Nice implementation */
		nice->maxRTT = 0;
		nice->numCong = 0;
		nice->thresholdRTT = 0;
	}
	/* Use normal slow start */
	else if (tcp_in_slow_start(tp))
		tcp_slow_start(tp, acked);
}

/* Extract info for TCP socket info provided via netlink. */
size_t tcp_nice_get_info(struct sock *sk, u32 ext, int *attr,
			 union tcp_cc_info *info)
{
	const struct nice *ca = inet_csk_ca(sk);

	if (ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		info->vegas.tcpv_enabled = ca->doing_nice_now;
		info->vegas.tcpv_rttcnt = ca->cntRTT;
		info->vegas.tcpv_rtt = ca->baseRTT;
		info->vegas.tcpv_minrtt = ca->minRTT;
        
        /* These fields aren't directly used by VEGASINFO, but we'll set them to 
           ensure proper compatibility. The struct tcpvegas_info doesn't have fields
           for maxRTT, thresholdRTT, or numCong, so we'll adapt as best we can */
        
		*attr = INET_DIAG_VEGASINFO;
		return sizeof(struct tcpvegas_info);
	}
    
    /* Return success with no info if the expected extension isn't available.
       This prevents the socket from being treated as invalid */
    if (ext == 0) {
        return 0;
    }
    
	return 0;
}
EXPORT_SYMBOL_GPL(tcp_nice_get_info);

static struct tcp_congestion_ops tcp_nice __read_mostly = {
	.init		= tcp_nice_init,
	.ssthresh	= tcp_reno_ssthresh,
	.undo_cwnd	= tcp_reno_undo_cwnd,
	.cong_avoid	= tcp_nice_cong_avoid,
	.pkts_acked	= tcp_nice_pkts_acked,
	.set_state	= tcp_nice_state,
	.cwnd_event	= tcp_nice_cwnd_event,
	.get_info	= tcp_nice_get_info,

	.owner		= THIS_MODULE,
	.name		= "nice",
};

static int __init tcp_nice_register(void)
{
	BUILD_BUG_ON(sizeof(struct nice) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_nice);
	printk(KERN_INFO "TCP Nice registered");
	return 0;
}

static void __exit tcp_nice_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_nice);
	printk(KERN_INFO "TCP Nice unregistered");
}

module_init(tcp_nice_register);
module_exit(tcp_nice_unregister);

MODULE_AUTHOR("Seyed Mohammad Hossein (Saam) Hosseini");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Nice congestion control (Venkataramani et al., OSDI 2002)");
