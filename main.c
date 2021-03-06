#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <time.h>


#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

static int in_cksum(unsigned short *buf, int sizeofpacket) {
    int nleft = sizeofpacket;
    int sum = 0;
    unsigned short *w = buf;
    unsigned short ans = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1) {
        *(unsigned char *) (&ans) = *(unsigned char *) w;
        sum += ans;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    ans = ~sum;
    return (ans);
}

static char *hostname = NULL;
static int Delay = 1000000;

main() {
    int sockfd,retval,n;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    char buf[10000]; // Set the buffer size to 10k
    int i;

    // Make the raw socket, but only make it look for ICMP packets.
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP); 
    if (sockfd < 0) {
        perror("sock:");
        exit(1);
    }
    clilen = sizeof(struct sockaddr_in);    
    while(1) {
        printf(" before recvfrom\n");   
        n = recvfrom(sockfd,buf,10000,0,(struct sockaddr *)&cliaddr,&clilen);
        printf(" rec'd %d bytes\n",n);

        struct iphdr *ip_hdr = (struct iphdr *)buf;

        printf("IP header is %d bytes.\n", ip_hdr->ihl*4);
        // notes: The IPv4 host sender is +12 bytes from the beggining of the IP packet.
        struct icmphdr *icmp_hdr = (struct icmphdr *)((char *)ip_hdr + (4 * ip_hdr->ihl));

        printf("ICMP msgtype=%d, code=%d\n", icmp_hdr->type, icmp_hdr->code);
        if(icmp_hdr->type == 8 && icmp_hdr->code == 0) {
            // We need to start the prediction round thing here.
            unsigned char IPSrc[4];
            for(i = 12; i < 12 + 4; i++) {
                // Copy the IP address from the IP header on the one that was sent, into this buffer
                IPSrc[i-12] = buf[i];
            }
            int pingsock, c;
            if ((pingsock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
                perror("ping: creating a raw socket to reply with! :(");
            } else {
                struct sockaddr_in pingaddr;
                struct icmp *pkt;
                struct hostent *h;
                char packet[n - 20];
                // Going to read out the old IPv4 packet and change the reply code and then
                // blank out the checksum.
                for(i = ip_hdr->ihl*4; i < n; i++) {
                    packet[i-ip_hdr->ihl*4] = buf[i];
                }
                packet[0] = 0;
                packet[2] = 0;
                packet[3] = 0;

                pingaddr.sin_family = AF_INET;
                char full[50];
                sprintf(full, "%d.%d.%d.%d", (int)IPSrc[0], (int)IPSrc[1], (int)IPSrc[2], (int)IPSrc[3]);
                fprintf(stderr, "ping: Got a ping from %s\n", full);
                if (!(h = gethostbyname(full))) {
                    fprintf(stderr, "ping: unknown host %s\n", full);
                    exit(1);
                }
                memcpy(&pingaddr.sin_addr, h->h_addr, sizeof(pingaddr.sin_addr));
                hostname = h->h_name;
                // Now we need to get the seq number and then inc it by one. Then put it back
                // in what we are going to respond with.
                packet[5]++;

                pkt = (struct icmp *) packet;
                pkt->icmp_cksum = in_cksum((unsigned short *) pkt, sizeof(packet));

                usleep(Delay); // Sleep for just enough time.
                Delay = Delay + 1000;
                printf("Waited for %d us before sending my prediciton\n", Delay);
                c = sendto(pingsock, packet, sizeof(packet), 0,
                    (struct sockaddr *) &pingaddr, sizeof(struct sockaddr_in));
                if (c < 0 || c != sizeof(packet)) {
                    if (c < 0)
                        perror("ping: sendto");
                    fprintf(stderr, "ping: write incomplete\n");
                }

                close(pingsock);
            }

        }
    }
}
