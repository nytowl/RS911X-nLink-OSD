/*******************************************************************************
* @file  bt_util.c
* @brief This file includes bt_util application which is used to send a frame to driver
* and receive response from the driver
*******************************************************************************
* # License
* <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
*******************************************************************************
*
* The licensor of this software is Silicon Laboratories Inc. Your use of this
* software is governed by the terms of Silicon Labs Master Software License
* Agreement (MSLA) available at
* www.silabs.com/about-us/legal/master-software-license-agreement. This
* software is distributed to you in Source Code format and is governed by the
* sections of the MSLA applicable to Source Code.
*
******************************************************************************/

#include <stdio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/types.h>
#include <linux/if.h>
#include <stdlib.h>
#include <linux/wireless.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include "bt_util.h"
#include <linux/genetlink.h>

/******************************************************************************
 * main()
 *****************************************************************************/
int main(int argc, char *argv[])
{
  int sfd;
  int ret   = 0;
  int cmdNo = -1;
  struct bb_rf_param_t bb_rf_params;

  memset(&bb_rf_params, 0, sizeof(bb_rf_params));

  if (argc < 2) {
    usage();
  } else if (argc <= 50) {
    cmdNo = getcmdnumber(argv[1]);
  }

  /* Open socket */
  sfd = socket_creation();
  if (sfd < 0) {
    ONEBOX_PRINT("Socket Creation Failed\n");
    return ONEBOX_STATUS_FAILURE;
  }

  switch (cmdNo) {
    case RSI_GET_BT_E2E_STATS: {
      if (argc == 3) {
        if (bt_e2e_stats(argv[2], sfd) == ONEBOX_STATUS_FAILURE) {
          return ONEBOX_STATUS_FAILURE;
        }
      } else {
        printf("Usage: ./bt_util bt_e2e_stats < filename >\n");
      }

    } break;
    case RSI_GET_BT_E2E_PERIOD_STATS: {
      if (argc == 4) {
        if (bt_e2e_period_stats(argv[2], atoi(argv[3]), sfd) == ONEBOX_STATUS_FAILURE) {
          return ONEBOX_STATUS_FAILURE;
        }
      } else {
        printf("Usage: ./bt_util bt_e2e_periodic_stats < filename > < duration >\n");
      }
    } break;
    default:
      break;
  }
  return ret;
}

int recv_bt_e2e_stat_from_drv(struct bt_stats_s *sta_info, int sfd)
{
  struct msghdr msg    = { 0 };
  struct nlmsghdr *nlh = NULL;
  struct iovec iov     = { 0 };
  struct sockaddr_nl dest_addr;
  nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(sizeof(struct bt_stats_s)));
  memset(nlh, 0, NLMSG_SPACE(sizeof(struct bt_stats_s)));
  nlh->nlmsg_len  = NLMSG_SPACE(sizeof(struct bt_stats_s));
  iov.iov_base    = (void *)nlh;
  iov.iov_len     = nlh->nlmsg_len;
  msg.msg_name    = (void *)&dest_addr;
  msg.msg_namelen = sizeof(dest_addr);
  msg.msg_iov     = &iov;
  msg.msg_iovlen  = 1;

  if (recvmsg(sfd, &msg, 0) < 0) {
    close(sfd);
    return -1;
  }
  memcpy(sta_info, NLMSG_DATA(nlh), sizeof(struct bt_stats_s));
  free(nlh);
  return 0;
}

int send_bt_e2e_stat_frame_to_drv(struct bb_rf_param_t bb_rf_params, int sfd)
{
  struct sockaddr_nl dest_addr;
  struct nlmsghdr *nlh        = NULL;
  struct rsi_nl_desc *nl_desc = NULL;
  int ret;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.nl_family = AF_NETLINK;
  dest_addr.nl_pid    = 0; /* For Linux Kernel */
  dest_addr.nl_groups = 0; /* unicast */
  nlh                 = (struct nlmsghdr *)malloc(NLMSG_SPACE(sizeof(struct bb_rf_param_t) + NL_DATA_DESC_SZ));
  memset(nlh, 0, NLMSG_SPACE(sizeof(struct bb_rf_param_t) + NL_DATA_DESC_SZ));
  nlh->nlmsg_len        = NLMSG_SPACE(sizeof(struct bb_rf_param_t) + NL_DATA_DESC_SZ);
  nl_desc               = (struct rsi_nl_desc *)NLMSG_DATA(nlh);
  nl_desc->desc_word[0] = BT_E2E_STAT;
  nl_desc->desc_word[1] = sizeof(struct bb_rf_param_t);
  nlh->nlmsg_type       = BT_PACKET;
  nlh->nlmsg_pid        = getpid();
  nlh->nlmsg_flags      = 0;
  memcpy(NLMSG_DATA(nlh) + NL_DATA_DESC_SZ, &bb_rf_params, sizeof(struct bb_rf_param_t));
  ret = common_send_mesg_wrapper(sfd, dest_addr, nlh);
  if (ret < 0)
    close(sfd);
  free(nlh);
  return ret;
}

int_32 bt_e2e_stats(uint_8 *file_name, int sfd)
{
  int first_time = 1;
  int count = 0, stop_bit = 0;
  int i;
  FILE *pFile;
  struct bt_stats_s *sta_info;
  struct bb_rf_param_t bb_rf_params;
  float rcv_percentage;

  memset(&bb_rf_params, 0, sizeof(bb_rf_params));

  pFile = fopen(file_name, "w");
  if (pFile == NULL) {
    printf("Unable to create a file\n");
    return ONEBOX_STATUS_FAILURE;
  }
  sta_info = (struct bt_stats_s *)malloc(100);

  while (1) {

    if (usleep(SLEEP_TIME) != 0) {
      printf("Unable to sleep\n");
      free(sta_info);
      break;
    }

    bb_rf_params.value = BT_E2E_STATS;

    if (send_bt_e2e_stat_frame_to_drv(bb_rf_params, sfd) < 0) {
      printf("Unable to perform BT E2E STATS request\n");
      free(sta_info);
      return ONEBOX_STATUS_FAILURE;
    } else {
      if (recv_bt_e2e_stat_from_drv(sta_info, sfd) < 0) {
        free(sta_info);
        return ONEBOX_STATUS_FAILURE;
      }
      if (first_time) {
        first_time = 0;
        free(sta_info);
        fflush(pFile);
        continue;
      }

      if (sta_info->crc_fail)
        rcv_percentage = (sta_info->crc_fail * 100.0) / (sta_info->crc_pass + sta_info->crc_fail);
      else
        rcv_percentage = 0;

      if ((count % 20) == 0) {
        printf("%s %9s %9s %9s %9s %9s %9s %9s %9s %8s %5s  %s\n",
               "crc_fail",
               "crc_pass",
               "tx_aborts",
               "cca_stk",
               "fls_rx_start",
               "cca_idle",
               "rx-phy_abort",
               "fifo_occupied",
               "tx_dones",
               "id_pkts_rcvd",
               "rssi",
               "packet_error_rate");
        fprintf(pFile,
                "\n%s %9s %9s %9s %9s %9s %9s %9s %9s %8s %5s  %s\n",
                "crc_fail",
                "crc_pass",
                "tx_aborts",
                "cca_stk",
                "fls_rx_start",
                "cca_idle",
                "rx-phy_abort",
                "fifo_occupied",
                "tx_dones",
                "id_pkts_rcvd",
                "rssi",
                "packet_error_rate");
      }
      printf("%2d %10d %10d %10d %10d %10d %10d %10d %12d %11d %11d %14f\n",
             sta_info->crc_fail,
             sta_info->crc_pass,
             sta_info->tx_aborts,
             sta_info->cca_stk,
             sta_info->fls_rx_start,
             sta_info->cca_idle,
             sta_info->rx_phy_abort,
             sta_info->fifo_occupied,
             sta_info->tx_dones,
             sta_info->id_pkts_rcvd,
             sta_info->rssi,
             rcv_percentage);
      fprintf(pFile,
              "%2d %10d %10d %10d %10d %10d %10d %10d %11d %11d %12d %14f\n",
              sta_info->crc_fail,
              sta_info->crc_pass,
              sta_info->tx_aborts,
              sta_info->cca_stk,
              sta_info->fls_rx_start,
              sta_info->cca_idle,
              sta_info->rx_phy_abort,
              sta_info->fifo_occupied,
              sta_info->tx_dones,
              sta_info->id_pkts_rcvd,
              sta_info->rssi,
              rcv_percentage);
      ++count;
      memset(sta_info, 0, 100);
    }
    fflush(pFile);
  }
  free(sta_info);
}

int_32 bt_e2e_period_stats(uint_8 *file_name, int period, int sfd)
{

  int first_time = 1;
  int count = 0, stop_bit = 0;
  int i;
  FILE *pFile;
  struct bt_stats_s *sta_info;
  struct bb_rf_param_t bb_rf_params;

  memset(&bb_rf_params, 0, sizeof(bb_rf_params));

  pFile = fopen(file_name, "w");
  if (pFile == NULL) {
    printf("Unable to create a file\n");
    return ONEBOX_STATUS_FAILURE;
  }
  sta_info = (struct bt_stats_s *)malloc(100);

  while (period) {

    if (usleep(SLEEP_TIME) != 0) {
      printf("Unable to sleep\n");
      free(sta_info);
      break;
    }

    bb_rf_params.value = BT_E2E_STATS;

    if (send_bt_e2e_stat_frame_to_drv(bb_rf_params, sfd) < 0) {
      printf("Unable to perform BT E2E PERIODIC STATS request\n");
      free(sta_info);
      return ONEBOX_STATUS_FAILURE;
    } else {
      if (recv_bt_e2e_stat_from_drv(sta_info, sfd) < 0) {
        free(sta_info);
        return ONEBOX_STATUS_FAILURE;
      }
      if (first_time) {
        first_time = 0;
        free(sta_info);
        fflush(pFile);
        continue;
      }

      if ((count % 20) == 0) {
        printf("%s %11s %11s %11s %14s %12s %12s %12s %11s %12s %8s\n",
               "crc_fail",
               "crc_pass",
               "tx_aborts",
               "cca_stk",
               "fls_rx_start",
               "cca_idle",
               "rx_phy_abort",
               "fifo_occupied",
               "tx_dones",
               "id_pkts_rcvd",
               "rssi");
        fprintf(pFile,
                "\n%s %11s %11s %11s %14s %12s %12s %12s %11s %12s %8s\n",
                "crc_fail",
                "crc_pass",
                "tx_aborts",
                "cca_stk",
                "fls_rx_start",
                "cca_idle",
                "rx-phy_abort",
                "fifo_occupied",
                "tx_dones",
                "id_pkts_rcvd",
                "rssi");
      }
      printf("%2d %12d %12d %12d %12d %12d %12d %12d %12d %12d %12d\n",
             sta_info->crc_fail,
             sta_info->crc_pass,
             sta_info->tx_aborts,
             sta_info->cca_stk,
             sta_info->fls_rx_start,
             sta_info->cca_idle,
             sta_info->rx_phy_abort,
             sta_info->fifo_occupied,
             sta_info->tx_dones,
             sta_info->id_pkts_rcvd,
             sta_info->rssi);
      fprintf(pFile,
              "%2d %12d %12d %12d %12d %12d %12d %12d %12d %12d %12d\n",
              sta_info->crc_fail,
              sta_info->crc_pass,
              sta_info->tx_aborts,
              sta_info->cca_stk,
              sta_info->fls_rx_start,
              sta_info->cca_idle,
              sta_info->rx_phy_abort,
              sta_info->fifo_occupied,
              sta_info->tx_dones,
              sta_info->id_pkts_rcvd,
              sta_info->rssi);
      ++count;
      memset(sta_info, 0, 100);
    }
    fflush(pFile);
    period--;
  }
  free(sta_info);
}

int getcmdnumber(char *command)
{
  if (!strcmp(command, "bt_e2e_stats")) {
    return RSI_GET_BT_E2E_STATS;
  }
  if (!strcmp(command, "bt_e2e_periodic_stats")) {
    return RSI_GET_BT_E2E_PERIOD_STATS;
  } else
    usage();
  return 0;
}

/** This function gives the usage of the onebox utility
 * @param  void
 * @return void 
 */
void usage()
{
  ONEBOX_PRINT("Usage: ./bt_util bt_e2e_stats < filename >\n");
  ONEBOX_PRINT("Usage: ./bt_util bt_e2e_periodic_stats < filename > < duration >\n");

  return;
}
