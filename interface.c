#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

volatile sig_atomic_t running = 1;
extern unsigned int if_nametoindex(const char *ifname);
static int tap_open(char *dev)

{
  struct ifreq ifr;

  int fd;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    return -1;
  }
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_NO_PI | IFF_TAP;
  if (*dev)
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", dev);
  if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
    goto failure;
  }

  strcpy(dev, ifr.ifr_name);
  return fd;
failure:

  close(fd);
  return -1;
}

int tapfd;
void signal_handler(int sig) {
  (void)sig;
  running = 0;
}
int set_ip_to_interface_tap(char *dev, char *ip) {
  struct ifreq ifr;
  int fd;
  struct sockaddr_in addr;
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return -1;
  }
  memset(&ifr, 0, sizeof(ifr));
  memset(&addr, 0, sizeof(addr));

  strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);

  addr.sin_family = AF_INET;
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    fprintf(stderr, "invalid ip address: %s\n", ip);
    close(fd);
    return -1;
  }
  memcpy(&ifr.ifr_addr, &addr, sizeof(addr));
  if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}
int main(int argc, char **argv) {

  char name[IFNAMSIZ];
  static char buf[65536];
  const char *nic = (argc > 1) ? argv[1] : "wlp0s20f3";
  unsigned long forwarded = 0;
  strncpy(name, "tap0", IFNAMSIZ - 1);
  name[IFNAMSIZ - 1] = '\0';
  tapfd = tap_open(name);
  if (tapfd < 0) {
    perror("tap_open (are you root?)");
    return -1;
  }
  if (set_ip_to_interface_tap(name, "192.0.2.1") < 0) {

    perror("set_ip");
    close(tapfd);
    return 1;
  }
  int fd_s = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (fd_s < 0) {
    perror("socket");
    close(tapfd);
    return 1;
  }
  struct sockaddr_ll addr = {0};
  addr.sll_family = AF_PACKET;
  addr.sll_protocol = htons(ETH_P_ALL);
  addr.sll_ifindex = if_nametoindex(nic);
  if (addr.sll_ifindex == 0) {
    fprintf(stderr, "unknown interface: %s\n", nic);
    close(fd_s);
    close(tapfd);
    return 1;
  }
  if (bind(fd_s, (struct sockaddr *)&addr, sizeof addr) < 0) {
    perror("bind");
    close(fd_s);
    close(tapfd);
    return 1;
  }
  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  struct pollfd fds[2] = {
      {.fd = fd_s, .events = POLLIN},
      {.fd = tapfd, .events = POLLIN},

  };
  while (running) {
    if (poll(fds, 2, 1000) < 0) {
      if (errno == EINTR)
        continue;
      perror("poll");
      break;
    }
    if (fds[0].revents & POLLIN) {
      struct sockaddr_ll from;
      socklen_t flen = sizeof from;
      ssize_t n =
          recvfrom(fd_s, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
      if (n > 0 && from.sll_pkttype != PACKET_OUTGOING) {
        if (write(tapfd, buf, n) < 0)
          perror("write tap");
        else
          forwarded++;
      }
    }
    if (fds[1].revents & POLLIN) {
      if (read(tapfd, buf, sizeof buf) < 0 && errno != EINTR &&
          errno != EAGAIN)
        perror("read tap");
    }
  }
  fprintf(stderr, "\nforwarded %lu frames\n", forwarded);
  close(fd_s);
  close(tapfd);
  return 0;
}
