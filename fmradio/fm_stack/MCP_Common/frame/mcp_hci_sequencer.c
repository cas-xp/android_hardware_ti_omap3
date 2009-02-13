/*
 * TI's FM Stack
 *
 * Copyright 2001-2008 Texas Instruments, Inc. - http://www.ti.com/
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and  
 * limitations under the License.
 */

#include "mcp_hci_sequencer.h"
#include "mcp_hal_memory.h"
#include "mcp_defs.h"

/* internal prototypes */
void _MCP_HciSeq_callback (BtHciIfClientEvent* pEvent);


/*---------------------------------------------------------------------------
 *            MCP_HciSeq_CreateSequence()
 *---------------------------------------------------------------------------
 *
 * Synopsis:  Prepares a HCI sequence to be used
 *
 */
void MCP_HciSeq_CreateSequence (MCP_HciSeq_Context *pContext, 
                                BtHciIfObj *hciIfObj, 
                                McpHalCoreId coreId)
{
    BtHciIfStatus   status;

    MCP_FUNC_START ("MCP_HciSeq_CreateSequence");

    /* nullify variables */
    pContext->bCancelFlag = MCP_FALSE;
    pContext->uCommandCount = 0;
    pContext->uCurrentCommandIdx = 0;
    /* register a HCI IF client */
    pContext->coreId = coreId;
    if(pContext->coreId == MCP_HAL_CORE_ID_BT)
    {
            status = BT_HCI_IF_RegisterClient(hciIfObj, _MCP_HciSeq_callback, &(pContext->handle));
        MCP_VERIFY_FATAL_NO_RETVAR ((BT_HCI_IF_STATUS_SUCCESS == status),
                                    ("MCP_HciSeq_CreateSequence: BT_HCI_IF_RegisterClient returned status %d",
                                     status));
    }

    MCP_FUNC_END ();
}

/*---------------------------------------------------------------------------
 *            MCP_HciSeq_DestroySequence()
 *---------------------------------------------------------------------------
 *
 * Synopsis:  destroys a HCI sequence that is no longer in use
 *
 */
void MCP_HciSeq_DestroySequence (MCP_HciSeq_Context *pContext)
{
    BtHciIfStatus   status;

    MCP_FUNC_START ("MCP_HciSeq_DestroySequence");

    /* de-register HCI IF client */
    status = BT_HCI_IF_DeregisterClient (&(pContext->handle));
    MCP_VERIFY_FATAL_NO_RETVAR ((BT_HCI_IF_STATUS_SUCCESS == status),
                                ("MCP_HciSeq_DestroySequence: BT_HCI_IF_DeregisterClient returned status %d",
                                 status));

    MCP_FUNC_END ();
}

/*---------------------------------------------------------------------------
 *            MCP_HciSeq_RunSequence()
 *---------------------------------------------------------------------------
 *
 * Synopsis:  Starts execution of an HCI sequence
 *
 */
 /*FM-VAC*/
BtHciIfStatus MCP_HciSeq_RunSequence (MCP_HciSeq_Context *pContext, 
                                const McpU32 uCommandCount,
                                const McpHciSeqCmd *pCommands,
                                McpBool bCallCbOnlyAfterLastCmd)
{
    /* sanity cehck */
    if (HCI_SEQ_MAX_CMDS_PER_SEQUENCE < uCommandCount)
    {
        return BT_HCI_IF_STATUS_FAILED;
    }
    pContext->bCallCBOnlyForLastCmd = bCallCbOnlyAfterLastCmd;

    /* copy commands */
    MCP_HAL_MEMORY_MemCopy ((McpU8 *)pContext->commandsSequence, 
                            (McpU8 *)pCommands,
                            uCommandCount*sizeof(McpHciSeqCmd));

    /* initialize new sequence */
    pContext->uCurrentCommandIdx = 0;

    /* if a sequence is running */
    if (pContext->uCommandCount > 0)
    {
        pContext->uCommandCount = uCommandCount;

        /* turn on cancel indication, indicating user CB for current command should not be called  */
        pContext->bCancelFlag = MCP_TRUE;

        return BT_HCI_IF_STATUS_PENDING;
    }
    /* sequence is not running at the moment */
    else
    {
        pContext->uCommandCount = uCommandCount;

        /* prepare first command (by calling the CB) */
        pContext->commandsSequence[ pContext->uCurrentCommandIdx ].fCommandPrepCB( 
            &(pContext->command), pContext->commandsSequence[ pContext->uCurrentCommandIdx ].pUserData );

        /* execute first command */
        if(pContext->coreId == MCP_HAL_CORE_ID_BT)
        {
        
                return BT_HCI_IF_SendHciCommand(pContext->handle,    
                                            pContext->command.eHciOpcode, 
                                            pContext->command.pHciCmdParms, 
                                            pContext->command.uhciCmdParmsLen,
                                            pContext->command.uCompletionEvent,
                                            (void*)pContext);
        }
        else if (pContext->coreId == MCP_HAL_CORE_ID_FM)
        {
            return FM_TRANSPORT_IF_SendFmVacCommand(    
                                        pContext->command.pHciCmdParms, 
                                        pContext->command.uhciCmdParmsLen,
                                        _MCP_HciSeq_callback,
                                        (void*)pContext);
        }
        return BT_HCI_IF_STATUS_INTERNAL_ERROR;
    }
}

/*---------------------------------------------------------------------------
 *            MCP_HciSeq_CancelSequence()
 *---------------------------------------------------------------------------
 *
 * Synopsis:  Stops execution of a HCI sequence
 *
 */
void MCP_HciSeq_CancelSequence (MCP_HciSeq_Context *pContext)
{
    /* if a sequence is running */
    if (0 < pContext->uCommandCount)
    {
        /* signal cancel flag - to avoid calling the CB */
        pContext->bCancelFlag = MCP_TRUE;

        /* nullify command count and current command index */
        pContext->uCommandCount = 0;
        pContext->uCurrentCommandIdx = 0;
    }
}

/*---------------------------------------------------------------------------
 *            _MCP_HciSeq_callback()
 *---------------------------------------------------------------------------
 *
 * Synopsis:  Internal callback used to trigger next command in the sequence
 *
 */
 /*FM-VAC*/
void _MCP_HciSeq_callback (BtHciIfClientEvent* pEvent)
{
    MCP_HciSeq_Context   *pContext = (MCP_HciSeq_Context*)pEvent->pUserData;
    BtHciIfStatus        tStatus = BT_HCI_IF_STATUS_INTERNAL_ERROR;

    /* CB is not called if previous sequence was canceled! */

    /* check again if cancel was requested during the callback */
    if (MCP_FALSE == pContext->bCancelFlag)
    {
        /* cancel was not requested - advance to next command */
        pContext->uCurrentCommandIdx++;
        
        /* if original CB exists */
        if (NULL != pContext->command.callback) 
        {
            if((!pContext->bCallCBOnlyForLastCmd)||
                ((pContext->bCallCBOnlyForLastCmd)&&
                    (pContext->uCurrentCommandIdx >= pContext->uCommandCount)))
            {
                /* set the user data in the completion event structure */
                pEvent->pUserData = pContext->command.pUserData;
        
                /* call original CB */
                pContext->command.callback (pEvent);
            }
        }   
    }
    else
    {
    
        /* signal cancel was done (by not calling the CB) */
        pContext->bCancelFlag = MCP_FALSE;
    }

    /* now check if more commands are available for processing */
    if (pContext->uCurrentCommandIdx < pContext->uCommandCount)
    {
        /* prepare next command (by calling the CB) */
        pContext->commandsSequence[ pContext->uCurrentCommandIdx ].fCommandPrepCB( 
            &(pContext->command), pContext->commandsSequence[ pContext->uCurrentCommandIdx ].pUserData );

        /* execute first command */
        if(pContext->coreId== MCP_HAL_CORE_ID_BT)
        {
        
            tStatus=BT_HCI_IF_SendHciCommand(pContext->handle,  
                                         pContext->command.eHciOpcode, 
                                         pContext->command.pHciCmdParms, 
                                         pContext->command.uhciCmdParmsLen,
                                         pContext->command.uCompletionEvent,
                                         (void*)pContext);       
        }
        else if (pContext->coreId == MCP_HAL_CORE_ID_FM)
        {
            tStatus=FM_TRANSPORT_IF_SendFmVacCommand(
                                             pContext->command.pHciCmdParms, 
                                             pContext->command.uhciCmdParmsLen,
                                             _MCP_HciSeq_callback,
                                             (void*)pContext);       
        }
        if (BT_HCI_IF_STATUS_PENDING != tStatus)
            {
                /* an error has occurred - set the user data in the completion event structure */
                pEvent->pUserData = pContext->command.pUserData;

                /* and call the CB */
                pContext->command.callback (pEvent);
            }
    }
    /* no more commands to execute - mark the sequence as not running */
    else
    {
        /* by nullifying number of commands and current command index */
        pContext->uCommandCount = 0;
        pContext->uCurrentCommandIdx = 0;
    }
}

