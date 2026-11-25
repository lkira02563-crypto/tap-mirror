/*
 * socket.c - test reader: listens on tap0 and prints the ethertype of every
 * frame, plus the IPv4 addresses when the frame carries IPv4.
 *
 * usage: sudo ./socket [interface]      (default: tap0)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *ifname = (argc > 1) ? argv[1] : "tap0";

  /* ETH_P_ALL: see every ethertype (ARP, IPv4, IPv6, ...) */
  int fd_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (fd_socket < 0) {
    perror("socket (are you root?)");
    return -1;
  }

  unsigned int ifindex = if_nametoindex(ifname);
  if (ifindex == 0) {
    fprintf(stderr, "unknown interface: %s\n", ifname);
    close(fd_socket);
    return -1;
  }

  struct sockaddr_ll addr;
  memset(&addr, 0, sizeof(addr));
  addr.sll_family = AF_PACKET;
  addr.sll_ifindex = ifindex;
  addr.sll_protocol = htons(ETH_P_ALL);
  if (bind(fd_socket, (struct sockaddr *)&addr, sizeof addr) < 0) {
    perror("bind");
    close(fd_socket);
    return -1;
  }

  uint8_t buffer[2048];

  while (1) {
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    ssize_t bytes = recvfrom(fd_socket, buffer, sizeof buffer, 0, NULL, NULL);

    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      perror("recvfrom");
      break;
    }
    if (bytes < (ssize_t)sizeof(struct ethhdr))
      continue;

    /* SOCK_RAW: the buffer starts with the 14-byte Ethernet header */
    struct ethhdr *eth = (struct ethhdr *)buffer;
    uint16_t type = ntohs(eth->h_proto);
    printf("len=%zd ethertype=0x%04x\n", bytes, type);

    if (type == ETH_P_IP &&
        bytes >= (ssize_t)(sizeof(struct ethhdr) + sizeof(struct iphdr))) {
      struct iphdr *ip = (struct iphdr *)(buffer + sizeof(struct ethhdr));
      inet_ntop(AF_INET, &ip->saddr, src, sizeof src);
      inet_ntop(AF_INET, &ip->daddr, dst, sizeof dst);
      printf("  %s -> %s protocol=%d\n", src, dst, ip->protocol);
    }
  }

  close(fd_socket);

  return 0;
}
