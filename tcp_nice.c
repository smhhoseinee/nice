#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tcp.h>
#include <net/tcp.h>

/* TCP Nice congestion control implementation */
static void tcp_nice_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
    struct tcp_sock *tp = tcp_sk(sk);
    
    if (!tcp_is_cwnd_limited(sk))
        return;
    
    printk(KERN_INFO "TCP Nice: cwnd=%u, ssthresh=%u\n", tp->snd_cwnd, tp->snd_ssthresh);

    /* Slow start if cwnd is below ssthresh */
    if (tp->snd_cwnd <= tp->snd_ssthresh)
        tcp_slow_start(tp, acked);
    else
        tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);
}

/* Slow start threshold calculation */
static u32 tcp_nice_ssthresh(struct sock *sk)
{
    const struct tcp_sock *tp = tcp_sk(sk);
    return max(tp->snd_cwnd >> 1U, 2U);
}

/* Initial congestion window */
static void tcp_nice_init(struct sock *sk)
{
    /* Standard initial value, typically 10 */
    tcp_sk(sk)->snd_cwnd = 9;
}

static u32 tcp_nice_undo_cwnd(struct sock *sk)
{
    /* Simple implementation - just return current cwnd */
    return tcp_sk(sk)->snd_cwnd;
}

/* Register TCP Nice congestion control algorithm */
static struct tcp_congestion_ops tcp_nice __read_mostly = {
    .name       = "nice",
    .owner      = THIS_MODULE,
    .cong_avoid = tcp_nice_cong_avoid,
    .ssthresh   = tcp_nice_ssthresh,
    .flags      = TCP_CONG_NON_RESTRICTED,
    .init       = tcp_nice_init,
    .undo_cwnd  = tcp_nice_undo_cwnd,
};

static int __init tcp_nice_register(void)
{
    return tcp_register_congestion_control(&tcp_nice);
}

static void __exit tcp_nice_unregister(void)
{
    tcp_unregister_congestion_control(&tcp_nice);
}

module_init(tcp_nice_register);
module_exit(tcp_nice_unregister);

MODULE_AUTHOR("Hossein");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Nice Congestion Control");


