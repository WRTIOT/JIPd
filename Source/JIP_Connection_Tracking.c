/****************************************************************************
 *
 * MODULE:             JIPv4 compatability daemon
 *
 * COMPONENT:          Connection tracking
 *
 * REVISION:           $Revision: 37697 $
 *
 * DATED:              $Date: 2011-12-06 14:41:22 +0000 (Tue, 06 Dec 2011) $
 *
 * AUTHOR:             Matt Redfearn
 *
 ****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.

 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libdaemon/daemon.h>

#include <JIP_Packets.h>

#include <JIP_Connection_Tracking.h>


extern int verbosity;



int JIP_ConnTrack_Request_Packet(tsJIPConnTrack *psJIPConnTrack, struct sockaddr_in6 *psDestAddress, const char *JIP_Packet, uint32_t u32Length)
{
    
    if (psJIPConnTrack->eState != E_CON_TRACK_STATE_IDLE)
    {
        daemon_log(LOG_ERR, "Unexpected command while waiting on response\n");
        return -1;
    }
    
    {
        tsJIP_MsgHeader *psJIP_MsgHeader = (tsJIP_MsgHeader *)JIP_Packet;
        
        if (psJIP_MsgHeader->u8Version != JIP_VERSION)
        {
            daemon_log(LOG_ERR, "Unexpected JIP Version (%d)\n", psJIP_MsgHeader->u8Version);
            return -1;
        }
        
        psJIPConnTrack->pcLastPacket = malloc(u32Length);
        
        if (!psJIPConnTrack->pcLastPacket)
        {
            return -1;
        }
        memcpy(psJIPConnTrack->pcLastPacket, JIP_Packet, u32Length);
        
        psJIPConnTrack->sLastRequestAddress = *psDestAddress;
    }
    
    psJIPConnTrack->eState = E_CON_TRACK_STATE_WAITING_RESPONSE;
    
    //printf("Set up connnection tracking for packet\n");

    return 0;
}


int JIP_ConnTrack_Response_Packet(tsJIPConnTrack *psJIPConnTrack, struct sockaddr_in6 *psSrcAddress, const char *JIP_Packet, uint32_t u32Length)
{
    if (psJIPConnTrack->eState != E_CON_TRACK_STATE_WAITING_RESPONSE)
    {
        daemon_log(LOG_ERR, "Unexpected response - maybe a trap?\n");
        return -1;
    }
    
    {
        tsJIP_MsgHeader *psJIP_MsgHeader        = (tsJIP_MsgHeader *)JIP_Packet;
        tsJIP_MsgHeader *psLastJIP_MsgHeader    = (tsJIP_MsgHeader *)psJIPConnTrack->pcLastPacket;
        
        if (psJIP_MsgHeader->u8Version != JIP_VERSION)
        {
            daemon_log(LOG_ERR, "Unexpected JIP Version (%d)\n", psJIP_MsgHeader->u8Version);
            return -1;
        }
        
        if (memcmp(&psSrcAddress->sin6_addr, &psJIPConnTrack->sLastRequestAddress.sin6_addr, sizeof(struct in6_addr)) != 0)
        {
            daemon_log(LOG_ERR, "Packet from unexpected node\n");
            return -1;
        }
        
        if (psJIP_MsgHeader->u8Handle == psLastJIP_MsgHeader->u8Handle)
        {
            if (verbosity >= LOG_DEBUG)
            {
                daemon_log(LOG_DEBUG, "Response to request packet received\n");
            }
            free(psJIPConnTrack->pcLastPacket);
            psJIPConnTrack->pcLastPacket = NULL;
            psJIPConnTrack->eState = E_CON_TRACK_STATE_IDLE;
        }
    }

    //printf("Got response for packet\n");
    return 0;
}



int JIP_ConnTrack_Timeout(tsJIPConnTrack *psJIPConnTrack, struct sockaddr_in6 *psSrcAddress, char *JIP_Packet, uint32_t *pu32Length)
{
    if (psJIPConnTrack->eState == E_CON_TRACK_STATE_IDLE)
    {
        if (verbosity >= LOG_DEBUG)
        {
            daemon_log(LOG_DEBUG, "No outstanding packet\n");
        }
        return 0;
    }
    
    if (verbosity >= LOG_DEBUG)
    {
        daemon_log(LOG_DEBUG, "Generating error packet for the outstanding request\n");
    }
#if 1
    *psSrcAddress = psJIPConnTrack->sLastRequestAddress;
    
    {
        tsJIP_MsgHeader *psJIP_MsgHeader = (tsJIP_MsgHeader *)JIP_Packet;
        tsJIP_MsgHeader *psLastJIP_MsgHeader    = (tsJIP_MsgHeader *)psJIPConnTrack->pcLastPacket;
        
        psJIP_MsgHeader->u8Version  = psLastJIP_MsgHeader->u8Version;
        psJIP_MsgHeader->u8Handle   = psLastJIP_MsgHeader->u8Handle;
        psJIP_MsgHeader->eCommand   = psLastJIP_MsgHeader->eCommand + 1;
        
        switch (psLastJIP_MsgHeader->eCommand)
        {
            case (E_JIP_COMMAND_GET_REQUEST):
            case (E_JIP_COMMAND_SET_REQUEST):
            case (E_JIP_COMMAND_TRAP_REQUEST):
            case (E_JIP_COMMAND_UNTRAP_REQUEST):
            {
                tsJIP_Msg_VarStatus *psJIP_Msg_VarStatus = (tsJIP_Msg_VarStatus *)psJIPConnTrack->pcLastPacket;
                tsJIP_Msg_VarDescriptionHeader *psJIP_Msg_VarDescriptionHeader = (tsJIP_Msg_VarDescriptionHeader *)JIP_Packet;
                
                psJIP_Msg_VarDescriptionHeader->u8MibIndex  = psJIP_Msg_VarStatus->u8MibIndex;
                psJIP_Msg_VarDescriptionHeader->u8VarIndex  = psJIP_Msg_VarStatus->u8VarIndex;
                psJIP_Msg_VarDescriptionHeader->eStatus     = E_JIP_ERROR_FAILED;
                *pu32Length = sizeof(tsJIP_Msg_VarDescriptionHeader);
                break;
            }
            
            case (E_JIP_COMMAND_QUERY_MIB_REQUEST):
            {
                tsJIP_Msg_QueryMibResponseHeader *psJIP_Msg_QueryMibResponseHeader = (tsJIP_Msg_QueryMibResponseHeader *)JIP_Packet;
                
                psJIP_Msg_QueryMibResponseHeader->eStatus   = E_JIP_ERROR_FAILED;
                psJIP_Msg_QueryMibResponseHeader->u8NumMibsReturned = 0;
                psJIP_Msg_QueryMibResponseHeader->u8NumMibsOutstanding = 0;
                *pu32Length = sizeof(tsJIP_Msg_QueryMibResponseHeader);
                break;
            }
            
            case (E_JIP_COMMAND_QUERY_VAR_REQUEST):
            {
                tsJIP_Msg_QueryVarRequest *psJIP_Msg_QueryVarRequest = (tsJIP_Msg_QueryVarRequest *)psJIPConnTrack->pcLastPacket;
                tsJIP_Msg_QueryVarResponseHeader *psJIP_Msg_QueryVarResponseHeader = (tsJIP_Msg_QueryVarResponseHeader *)JIP_Packet;
                
                psJIP_Msg_QueryVarResponseHeader->eStatus   = E_JIP_ERROR_FAILED;
                psJIP_Msg_QueryVarResponseHeader->u8MibIndex = psJIP_Msg_QueryVarRequest->u8MibIndex;
                psJIP_Msg_QueryVarResponseHeader->u8NumVarsReturned = 0;
                psJIP_Msg_QueryVarResponseHeader->u8NumVarsOutstanding = 0;
                *pu32Length = sizeof(tsJIP_Msg_QueryVarResponseHeader);
                break;
            }
            
            default:
                daemon_log(LOG_ERR, "Unhandled packet type (%d)\n", psLastJIP_MsgHeader->eCommand);
                psJIPConnTrack->eState = E_CON_TRACK_STATE_IDLE;
                return 0;
        }
    }
    
    free(psJIPConnTrack->pcLastPacket);
    psJIPConnTrack->pcLastPacket = NULL;
    psJIPConnTrack->eState = E_CON_TRACK_STATE_IDLE;
#endif
    return 1;
}



