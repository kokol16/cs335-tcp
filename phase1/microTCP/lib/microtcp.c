/*
 * microtcp, a lightweight implementation of TCP for teaching,
 * and academic purposes.
 *
 * Copyright (C) 2015-2017  Manolis Surligas <surligas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file microtcp.c 
 * @author Manos Chatzakis (chatzakis@ics.forth.gr)
 * @author George Kokolakis (gkokol@ics.forth.gr)
 * @brief microTCP implementation for the undergraduate course cs335a
 * @version 1.0 - Phase 2
 * @date 2020-11-18
 * 
 * @copyright Copyright (c) 2020
 * 
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "microtcp.h"
#include "common.h"
#include "../utils/crc32.h"

/*
1) flow window  = 0 -- den exw idea
2) test retrans -- todo
4) test bandwidth  -- its ok
5) check ack/seq evaluations
*/

microtcp_sock_t microtcp_socket(int domain, int type, int protocol)
{
  microtcp_sock_t new_socket;
  memset(&new_socket, 0, sizeof(new_socket));

  /*MicroTCP library only uses UDP and IPv4*/
  protocol = 0;
  type = SOCK_DGRAM;
  domain = AF_INET;

  if ((new_socket.sd = socket(domain, type, protocol)) < 0)
  {
    new_socket.state = INVALID;
    return new_socket;
  }

  new_socket.state = CREATED;
  return new_socket;
}

int microtcp_bind(microtcp_sock_t *socket, const struct sockaddr *address, socklen_t address_len)
{
  int ret_val;

  if (socket == NULL)
    return -1;

  ret_val = bind(socket->sd, address, address_len);
  if (ret_val < 0)
  {
    socket->state = INVALID;
    return -1;
  }

  /*If bind succeeds the server listens to the port given*/
  socket->state = LISTEN;
  return ret_val;
}

int microtcp_connect(microtcp_sock_t *socket, const struct sockaddr *address, socklen_t address_len)
{
  microtcp_header_t header_1, header_2, header_3;

  if (socket == NULL)
    return -1;

  socket->address = (struct sockaddr *)address;
  socket->address_len = address_len;

  /*Sending first SYN packet with appropriate fields*/
  memset(&header_1, 0, sizeof(microtcp_header_t));
  header_1.seq_number = get_random_int(1, 49);
  header_1.control = set_control_bits(0, 0, 1, 0);
  header_1.window = MICROTCP_WIN_SIZE;
  header_1.checksum = crc32(&header_1, sizeof(microtcp_header_t));

  convert_to_network_header(&header_1);
  microtcp_raw_send(socket, &header_1, sizeof(header_1), 0);
  convert_to_local_header(&header_1);

  if (DEBUG)
  {
    printf("Packet 1:\n");
    print_header(header_1);
  }

  /*Recieving SYN ACK packet*/
  microtcp_raw_recv(socket, &header_2, sizeof(header_2), MSG_WAITALL);
  convert_to_local_header(&header_2);

  if (DEBUG)
  {
    printf("Packet 2:\n");
    print_header(header_2);
  }

  if (!validate_header(&header_2, header_1.seq_number, 0))
    return -1;

  /*Sending the third ACK packet*/
  memset(&header_3, 0, sizeof(header_3));
  header_3.ack_number = header_2.seq_number + 1;
  header_3.control = set_control_bits(1, 0, 0, 0);
  header_3.seq_number = header_2.ack_number;
  header_3.window = MICROTCP_WIN_SIZE;
  header_3.checksum = crc32(&header_3, sizeof(microtcp_header_t));

  convert_to_network_header(&header_3);
  microtcp_raw_send(socket, &header_3, sizeof(header_3), 0);
  convert_to_local_header(&header_3);

  if (DEBUG)
  {
    printf("Packet 3:\n");
    print_header(header_3);
  }

  /*If no error occured, connection is established*/
  socket->state = ESTABLISHED;

  socket->ack_number = header_3.ack_number;
  socket->seq_number = header_3.seq_number + 1;

  socket->init_win_size = header_2.window;
  socket->curr_win_size = header_2.window;

  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;

  socket->recvbuf = malloc(sizeof(uint8_t) * MICROTCP_RECVBUF_LEN);
  socket->buf_fill_level = 0;

  return 0;
}

int microtcp_accept(microtcp_sock_t *socket, struct sockaddr *address, socklen_t address_len)
{
  microtcp_header_t *header_ptr;
  microtcp_header_t header_1, header_2, header_3;

  if (socket == NULL)
    return -1;

  socket->address = address;
  socket->address_len = address_len;

  /*Waiting the first packet to arrive*/
  microtcp_raw_recv(socket, &header_1, sizeof(header_1), MSG_WAITALL);
  convert_to_local_header(&header_1);

  if (DEBUG)
  {
    printf("Packet 1:\n");
    print_header(header_1);
  }

  if (!validate_header(&header_1, 0, 1))
  {
    return -1;
  }

  /*Sending the second packet*/
  memset(&header_2, 0, sizeof(header_2));
  header_2.ack_number = header_1.seq_number + 1;
  header_2.seq_number = get_random_int(50, 99);
  header_2.control = set_control_bits(1, 0, 1, 0);
  header_2.window = MICROTCP_WIN_SIZE;
  header_2.checksum = crc32(&header_2, sizeof(microtcp_header_t));

  convert_to_network_header(&header_2);
  microtcp_raw_send(socket, &header_2, sizeof(header_2), 0);
  convert_to_local_header(&header_2);

  if (DEBUG)
  {
    printf("Packet 2:\n");
    print_header(header_2);
  }

  /*Waiting the third packet*/
  microtcp_raw_recv(socket, &header_3, sizeof(header_3), MSG_WAITALL);
  convert_to_local_header(&header_3);

  if (DEBUG)
  {
    printf("Packet 3:\n");
    print_header(header_3);
  }

  if (!validate_header(&header_3, header_2.seq_number, 0))
    return -1;

  socket->state = ESTABLISHED;

  socket->seq_number = header_3.ack_number; /*At this point, this assignment is equal to: socket->seq_number++;*/
  socket->ack_number = header_3.seq_number + 1;

  socket->init_win_size = header_3.window;
  socket->curr_win_size = header_3.window;

  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;

  socket->recvbuf = malloc(sizeof(uint8_t) * MICROTCP_RECVBUF_LEN);
  socket->buf_fill_level = 0;

  return 0;
}

int microtcp_shutdown(microtcp_sock_t *socket, int how)
{
  int ret_val;

  microtcp_header_t *header_ptr;
  microtcp_header_t header_1, header_2, header_3, header_4;

  /*Server side*/
  if (socket->state == CLOSING_BY_PEER)
  {
    socket->seq_number += 1;
    memset(&header_3, 0, sizeof(header_3));
    header_3.seq_number = socket->seq_number;
    header_3.ack_number = socket->ack_number;
    header_3.control = set_control_bits(1, 0, 0, 1);
    header_3.checksum = crc32(&header_3, sizeof(microtcp_header_t));

    convert_to_network_header(&header_3);
    microtcp_raw_send(socket, &header_3, sizeof(header_3), 0);
    convert_to_local_header(&header_3);

    if (DEBUG)
    {
      printf("Header 3:\n");
      print_header(header_3);
    }
    microtcp_raw_recv(socket, &header_4, sizeof(header_4), MSG_WAITALL);
    convert_to_local_header(&header_4);

    if (DEBUG)
    {
      printf("Header 4:\n");
      print_header(header_4);
    }

    if (!validate_header(&header_4, header_3.seq_number, 0))
      return -1;

    socket->seq_number = header_4.ack_number;
    socket->ack_number = header_4.seq_number + 1;

    socket->state = CLOSED;
    close(socket->sd);
  }
  else
  { //client

    socket->ack_number += 1;

    memset(&header_1, 0, sizeof(header_1));
    header_1.ack_number = (socket->ack_number);
    header_1.seq_number = (socket->seq_number);
    header_1.control = (set_control_bits(1, 0, 0, 1));
    header_1.checksum = crc32(&header_1, sizeof(microtcp_header_t));

    convert_to_network_header(&header_1);
    microtcp_raw_send(socket, &header_1, sizeof(header_1), 0);
    convert_to_local_header(&header_1);

    if (DEBUG)
    {
      printf("Header 1 ???:\n");
      print_header(header_1);
    }

    microtcp_raw_recv(socket, &header_2, sizeof(header_2), MSG_WAITALL);
    convert_to_local_header(&header_2);

    if (DEBUG)
    {
      printf("Header 2 ???:\n");
      print_header(header_2);
    }

    if (!validate_header(&header_2, header_1.seq_number, 0))
      return -1;

    socket->state = CLOSING_BY_HOST;

    microtcp_raw_recv(socket, &header_3, sizeof(header_3), MSG_WAITALL);
    convert_to_local_header(&header_3);

    if (DEBUG)
    {
      printf("Header 3:\n");
      print_header(header_3);
    }

    if (!validate_header(&header_3, header_1.seq_number, 0))
      return -1;

    memset(&header_4, 0, sizeof(header_4));
    header_4.ack_number = header_3.seq_number + 1;
    header_4.seq_number = header_3.ack_number;
    header_4.control = set_control_bits(1, 0, 0, 0);
    header_4.checksum = crc32(&header_4, sizeof(microtcp_header_t));

    convert_to_network_header(&header_4);
    microtcp_raw_send(socket, &header_4, sizeof(header_4), 0);
    convert_to_local_header(&header_4);

    if (DEBUG)
    {
      printf("Header 4:\n");
      print_header(header_4);
    }

    socket->seq_number = header_4.seq_number + 1;
    socket->ack_number = header_4.ack_number;

    socket->state = CLOSED;
    close(socket->sd);
  }

  free(socket->recvbuf);
  return 0;
}

ssize_t microtcp_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags)
{
  ssize_t bytes_sent, data_sent, recv, bytes_to_sent, total_bytes_sent = 0; /*bytes_sent is data+header*/
  size_t rem, chunk_size, remaining_bytes, total_length = length + sizeof(microtcp_header_t);

  int chunks, i, duplicate_counter = 0, ret_chunk = 0, ret_ack = 0, isTripleDuplicate = 0, isTimeout = 0;

  microtcp_header_t header, ack_header;

  void *packet;
  struct timeval timeout;

  if (socket == NULL)
    return -1;

  /*MSS does not contain the header size*/
  packet = malloc((MICROTCP_MSS + sizeof(microtcp_header_t)) * sizeof(char));

  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;

  remaining_bytes = length;
  while (total_bytes_sent < length)
  {

    bytes_to_sent = min(remaining_bytes, socket->curr_win_size, socket->cwnd);
    chunks = bytes_to_sent / MICROTCP_MSS; /*how many segments*/

    if (DEBUG_TCP_FLOW)
    {
      printf("======================\n");
      printf("Remaining Bytes: %d\nFlow Control Window: %d\nCongestion Control Window: %d\n", remaining_bytes, socket->curr_win_size, socket->cwnd);
      printf("Transmission round (bytes to sent) : %d\n", bytes_to_sent);
    }

    /* If server's recieve buffer is full, the client sents empty packets till the buffer has space*/
    if (socket->curr_win_size == 0)
    {

      if (DEBUG_TCP_FLOW)
        printf("Recieve buffer is full, waiting...\n");

      sleep_random_time();

      memset(&header, 0, sizeof(header));
      initiliaze_default_header(&header, *socket, 0);

      header.seq_number = socket->seq_number + 1;
      header.ack_number = socket->ack_number + 1;

      convert_to_network_header(&header);
      sendto(socket->sd, &header, sizeof(header), flags, socket->address, socket->address_len);
      convert_to_local_header(&header);

      recv = microtcp_raw_recv(socket, &ack_header, sizeof(ack_header), MSG_WAITALL);
      convert_to_local_header(&ack_header);

      if (!validate_header(&ack_header, header.seq_number, 1))
      {
        if (DEBUG_TCP_FLOW)
          printf("Corrupt non payload ack...\n");

        continue;
      }

      /*Current window size is updated after the chunk processing.*/
    }

    /*Sending the complete segments first*/
    for (i = 0; i < chunks; i++)
    {

      memset(&header, 0, sizeof(header));
      initiliaze_default_header(&header, *socket, MICROTCP_MSS);

      /*
        Client expects the next ack packet. 
        The next ack is always socket->ack + 1 if there
        are no losses during the flow.
        Thus, first ack is socket->ack_number+1, second is 
        socket->ack+2 etc.
        Same logic applies for socket->seq_number.
        
        NOTE: socket's ack and seq number are updated at the end of
        transmission round.
      */
      header.ack_number = socket->ack_number + 1 + i;
      header.seq_number = socket->seq_number + (i * MICROTCP_MSS);

      memcpy(packet, &header, sizeof(microtcp_header_t));
      memcpy((packet + sizeof(microtcp_header_t)), (buffer + (i * MICROTCP_MSS) * sizeof(char)), MICROTCP_MSS); //check again
      header.checksum = crc32(packet, MICROTCP_MSS + sizeof(header));
      convert_to_network_header(&header);
      memcpy(packet, &header, sizeof(header));
      sendto(socket->sd, packet, MICROTCP_MSS + sizeof(header), flags, socket->address, socket->address_len);
      convert_to_local_header(&header);

      if (DEBUG_DATA)
      {
        printf("Send: Header sent:\n");
        print_header(header);
      }
    }

    /*
      Sending the last (smaller) packet.
      rem = remaining bytes
    */
    rem = bytes_to_sent % MICROTCP_MSS;
    if (rem > 0)
    {
      memset(&header, 0, sizeof(header));
      initiliaze_default_header(&header, *socket, rem);

      /*Same logic as above*/
      header.ack_number = socket->ack_number + 1 + chunks;
      header.seq_number = socket->seq_number + (MICROTCP_MSS * chunks);

      memcpy(packet, &header, sizeof(microtcp_header_t));
      memcpy((packet + sizeof(microtcp_header_t)), (buffer + (MICROTCP_MSS * chunks) * sizeof(char)), rem); //check again

      header.checksum = crc32(packet, MICROTCP_MSS + sizeof(header));
      convert_to_network_header(&header);
      memcpy(packet, &header, sizeof(header));
      sendto(socket->sd, packet, rem + sizeof(header), flags, socket->address, socket->address_len);
      convert_to_local_header(&header);

      chunks++;

      if (DEBUG_DATA)
      {
        printf("Send: Header sent:\n");
        print_header(header);
      }
    }

    /*Setting the timer*/
    timeout.tv_sec = 0;
    timeout.tv_usec = MICROTCP_ACK_TIMEOUT_US;

    /*
      Segments are sent.
      At this point, client waits for the
      acknowledgment packets.  
    */
    for (i = 0; i < chunks; i++)
    {

      if (setsockopt(socket->sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0)
      {
        perror("setsockopt");
      }

      recv = microtcp_raw_recv(socket, &ack_header, sizeof(ack_header), MSG_WAITALL);
      convert_to_local_header(&ack_header);

      //checksum ack_header
      if (!validate_header(&ack_header, 0, 1))
      {
        if (DEBUG_TCP_FLOW)
          printf("Corrupted ACK. Retransmitting...\n");
        continue;
      }

      /*Timer expired. Timeout scenario.*/
      if (recv < 0)
      {
        if (DEBUG_TCP_FLOW)
          printf("Timeout!\n");

        isTimeout = 1;
        break;
      }

      /*If the recieved header has ack number corresponding to an older packet, we have duplicate ACK*/
      if (ack_header.ack_number <= socket->seq_number + (i * MICROTCP_MSS)) //This could be better
      {
        if (DEBUG_TCP_FLOW)
        {
          printf("\033[0;31m");
          printf("Duplicate ACK!\n");
          printf("\033[0m");
        }

        /*We have a duplicate ACK, but for a newer segment.*/
        if (ack_header.ack_number > ret_ack)
        {
          duplicate_counter = 0;
        }

        duplicate_counter++;

        /*New duplicate ACK*/
        if (duplicate_counter == 1)
        {
          ret_ack = ack_header.ack_number;
          ret_chunk = i;
        }

        /*Triple duplicate ACK*/
        if (duplicate_counter == 3)
        {
          isTripleDuplicate = 1;
          break;
        }
      }

      if (DEBUG_DATA)
      {
        printf("Send: Header recieved (ACK):\n");
        print_header(ack_header);
      }
    }

    /*The last recieved header indicates the server's recieve buffer situation*/
    socket->curr_win_size = ack_header.window;

    /*Actions of triple duplicate ACK.*/
    if (isTripleDuplicate || duplicate_counter > 0)
    {

      /*Congestion control algorithm*/
      socket->ssthresh = socket->cwnd / 2;
      socket->cwnd = socket->cwnd / 2 + 1;
      duplicate_counter = 0;
      isTripleDuplicate = 0;
      
      remaining_bytes -= MICROTCP_MSS * ret_chunk;
      total_bytes_sent += MICROTCP_MSS * ret_chunk; //?
      socket->seq_number += MICROTCP_MSS * ret_chunk;
      
      socket->ack_number += ret_chunk; //ack_header.seq_number; //+ 1; /*This okay, as the transmission is succesful*/

      duplicate_counter = 0;
      ret_ack = 0;
      ret_chunk = 0;

      if (DEBUG_TCP_FLOW)
      {
        printf("\033[0;32m");
        printf("Triple Duplicate ACK or Pending ACK. Taking actions...\n");
        printf("New ssthresh: %d\nNew cwnd: %d\n", socket->ssthresh, socket->cwnd);
        printf("\033[0m");
      }

      continue; /*Retransmit*/ 
    }

    /*Actions upon timer expiration*/
    if (isTimeout)
    {
      /*Congestion control algorithm*/
      socket->ssthresh = socket->cwnd / 2;
      socket->cwnd = min(MICROTCP_MSS, socket->ssthresh, socket->ssthresh + MICROTCP_MSS);
      isTimeout = 0;

      if (DEBUG_TCP_FLOW)
      {
        printf("Timer Expired. Taking actions...\n");
        printf("New ssthresh: %d\nNew cwnd: %d\n", socket->ssthresh, socket->cwnd);
      }

      continue; /*Retransmit*/
    }

    /*Increment congestion window exponentially in slow start*/
    if (socket->cwnd <= socket->ssthresh)
    {
      if (DEBUG_TCP_FLOW)
        printf("Slow Start Phase\n");

      socket->cwnd *= 2;
    }

    /*Increment congestion window linearly in congestion avoidance*/
    else
    {
      if (DEBUG_TCP_FLOW)
        printf("Congestion Avoidance Phase\n");

      socket->cwnd += MICROTCP_MSS;
    }

    remaining_bytes -= bytes_to_sent;
    total_bytes_sent += bytes_to_sent;

    /*Upon successful transmission increment the socket's seq/ack*/
    socket->seq_number += bytes_to_sent;
    socket->ack_number += chunks; //ack_header.seq_number; //+ 1; /*This okay, as the transmission is succesful*/
    duplicate_counter = 0;
    ret_ack = 0;
    ret_chunk = 0;

    if (DEBUG_TCP_FLOW)
      printf("======================\n");
  }

  free(packet);
  return total_bytes_sent;
}

ssize_t microtcp_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
  ssize_t bytes_recieved, data_size, rem_size, skip;
  microtcp_header_t header, ack_header;

  void *packet;

  if (socket == NULL)
    return -1;

  while (1)
  {
    memset(&header, 0, sizeof(header));
    packet = malloc((MICROTCP_MSS + sizeof(header)) * sizeof(char));

    /*We do not use a while loop, because our model supposes that this function is used always in while loopss*/
    bytes_recieved = recvfrom(socket->sd, packet, MICROTCP_MSS + sizeof(header), flags, (socket->address), &socket->address_len);

    /*Generic error check*/
    if (bytes_recieved == -1)
    {
      free(packet);
      printf("Error recieving packet\n");
      return -1;
    }

    data_size = bytes_recieved - sizeof(header);
    memcpy(&header, packet, sizeof(header));
    convert_to_local_header(&header);

    skip = skip_ack();
    skip = 1;
    
    if (skip && socket->ack_number == header.seq_number && validate_checksum(&header, packet, bytes_recieved))
    {
      /*Connection finalization case, FIN-ACK packet*/
      if (get_bit(header.control, 0) && get_bit(header.control, 3))
      {
        socket->state = CLOSING_BY_PEER;
        socket->seq_number = header.ack_number; /*As this case is right, this is equal to socket->seq_number++*/
        socket->ack_number += 1;                /*This is okay because next packets contain only headers*/

        memset(&ack_header, 0, sizeof(header));
        ack_header.seq_number = socket->seq_number;
        ack_header.ack_number = socket->ack_number;
        ack_header.control = set_control_bits(1, 0, 0, 0);
        ack_header.checksum = crc32(&ack_header, sizeof(microtcp_header_t));

        convert_to_network_header(&ack_header);
        microtcp_raw_send(socket, &ack_header, sizeof(header), 0);
        convert_to_local_header(&ack_header);

        if (DEBUG)
        {
          printf("Header 1:\n");
          print_header(header);
          printf("Header 2:\n");
          print_header(ack_header);
        }

        free(packet);
        return 0;
      }
      else
      {
        /*Sending proper ACK*/
        data_size = header.data_len;

        /*Calculating the remaining buffer space*/
        if (socket->buf_fill_level + data_size < MICROTCP_RECVBUF_LEN)
        {
          memcpy((socket->recvbuf + socket->buf_fill_level), (packet + sizeof(microtcp_header_t)), data_size);
          socket->buf_fill_level += data_size;
          rem_size = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
        }
        else
        {
          rem_size = 0;
        }

        socket->seq_number = header.ack_number; /*As this case is right, this is equal to socket->seq_number++*/
        socket->ack_number = header.seq_number + data_size;

        memset(&ack_header, 0, sizeof(header));
        ack_header.seq_number = socket->seq_number;
        ack_header.ack_number = socket->ack_number;
        ack_header.window = rem_size;
        ack_header.control = set_control_bits(1, 0, 0, 0);

        convert_to_network_header(&ack_header);
        microtcp_raw_send(socket, &ack_header, sizeof(header), 0);
        convert_to_local_header(&ack_header);

        memcpy(buffer, socket->recvbuf, socket->buf_fill_level);
        memset(socket->recvbuf, 0, socket->buf_fill_level);
        socket->buf_fill_level = 0;

        if (DEBUG_DATA)
        {
          printf("Recieve: Header recieved:\n");
          print_header(header);

          printf("Recieve: Header sent (ACK):\n");
          print_header(ack_header);
        }
      }
      break;
      //return data_size;
    }
    else
    {
      //socket->seq_number++;

      /*Sending duplicate ACK*/
      memset(&ack_header, 0, sizeof(header));
      rem_size = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
      if (rem_size < 0)
        rem_size = 0;

      ack_header.window = rem_size;
      ack_header.seq_number = socket->seq_number + 1;
      ack_header.ack_number = socket->ack_number;
      ack_header.control = set_control_bits(1, 0, 0, 0);

      if (DEBUG_TCP_FLOW)
        printf("Sending duplicate ACK...\n");

      convert_to_network_header(&ack_header);
      microtcp_raw_send(socket, &ack_header, sizeof(header), 0);
      convert_to_local_header(&ack_header);
    }
  }
  free(packet);
  return data_size;
}

ssize_t microtcp_raw_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
  return recvfrom(socket->sd, buffer, length, flags, (socket->address), &socket->address_len);
}

ssize_t microtcp_raw_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags)
{
  return sendto(socket->sd, buffer, length, flags, (socket->address), socket->address_len);
}
