/****************************************************************************
 * modules/lte/altcom/api/lte/lte_getdynamicpsm.c
 *
 *   Copyright 2019, 2020 Sony Semiconductor Solutions Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Sony Semiconductor Solutions Corporation nor
 *    the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "lte/lte_api.h"
#include "buffpoolwrapper.h"
#include "evthdlbs.h"
#include "apicmdhdlrbs.h"
#include "altcombs.h"
#include "altcom_callbacks.h"
#include "apicmd_getdynamicpsm.h"
#include "lte_getdynamicpsm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define REQ_DATA_LEN (0)
#define RES_DATA_LEN (sizeof(struct apicmd_cmddat_getdynamicpsmres_s))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: getdynamicpsm_status_chg_cb
 *
 * Description:
 *   Notification status change in processing get PSM dynamic parameter.
 *
 * Input Parameters:
 *  new_stat    Current status.
 *  old_stat    Preview status.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static int32_t getdynamicpsm_status_chg_cb(int32_t new_stat, int32_t old_stat)
{
  if (new_stat < ALTCOM_STATUS_POWER_ON)
    {
      DBGIF_LOG2_INFO("getdynamicpsm_status_chg_cb(%d -> %d)\n", old_stat, new_stat);
      altcomcallbacks_unreg_cb(APICMDID_GET_DYNAMICPSM);

      return ALTCOM_STATUS_REG_CLR;
    }

  return ALTCOM_STATUS_REG_KEEP;
}

/****************************************************************************
 * Name: get_dynamicpsm_job
 *
 * Description:
 *   This function is an API callback for get PSM dynamic parameter.
 *
 * Input Parameters:
 *  arg    Pointer to received event.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void get_dynamicpsm_job(FAR void *arg)
{
  int32_t                                     ret;
  FAR struct apicmd_cmddat_getdynamicpsmres_s *data;
  get_dynamic_psm_param_cb_t                  callback;
  int32_t                                     result = LTE_RESULT_ERROR;
  lte_psm_setting_t                           psm_set;

  data = (FAR struct apicmd_cmddat_getdynamicpsmres_s *)arg;

  ret = altcomcallbacks_get_unreg_cb(APICMDID_GET_DYNAMICPSM,
                                     (void **)&callback);
  if ((0 != ret) || (!callback))
    {
      DBGIF_LOG_ERROR("Unexpected!! callback is NULL.\n");
    }
  else
    {
      memset(&psm_set, 0, sizeof(lte_psm_setting_t));

      if (LTE_RESULT_OK == data->result)
        {
          altcombs_set_psm(&data->set, &psm_set);

          ret = altcombs_check_psm(&psm_set);
          if (0 > ret)
            {
              DBGIF_LOG1_ERROR("altcombs_check_psm() failed: %d\n", ret);
            }
          else
            {
              result = LTE_RESULT_OK;
            }
        }

      callback(result, &psm_set);
    }

  /* In order to reduce the number of copies of the receive buffer,
   * bring a pointer to the receive buffer to the worker thread.
   * Therefore, the receive buffer needs to be released here. */

  altcom_free_cmd((FAR uint8_t *)arg);

  /* Unregistration status change callback. */

  altcomstatus_unreg_statchgcb(getdynamicpsm_status_chg_cb);
}

/****************************************************************************
 * Name: lte_getcurrentpsm_impl
 *
 * Description:
 *   Get current PSM settings.
 *
 * Input Parameters:
 *   settings Current PSM settings.
 *   callback Callback function to notify when getting current PSM settings
 *            is completed.
 *            If the callback is NULL, operates with synchronous API,
 *            otherwise operates with asynchronous API.
 *
 * Returned Value:
 *   On success, 0 is returned.
 *   On failure, negative value is returned according to <errno.h>.
 *
 ****************************************************************************/

static int32_t lte_getcurrentpsm_impl(lte_psm_setting_t *settings,
                                      get_current_psm_cb_t callback)
{
  int32_t                                  ret;
  FAR uint8_t                             *reqbuff    = NULL;
  FAR uint8_t                             *presbuff   = NULL;
  struct apicmd_cmddat_getdynamicpsmres_s  resbuff;
  uint16_t                                 resbufflen = RES_DATA_LEN;
  uint16_t                                 reslen     = 0;
  int                                      sync       = (callback == NULL);
  uint16_t                                 cmdid = 0;
  int                                      protocolver = 0;

  /* Check input parameter */

  if (!settings && !callback)
    {
      DBGIF_LOG_ERROR("Input argument is NULL.\n");
      return -EINVAL;
    }

  /* Check LTE library status */

  ret = altcombs_check_poweron_status();
  if (0 > ret)
    {
      return ret;
    }

  cmdid = apicmdgw_get_cmdid(APICMDID_GET_DYNAMICPSM);
  if (cmdid == APICMDID_UNKNOWN)
    {
      return -ENETDOWN;
    }

  protocolver = apicmdgw_get_protocolversion();
  if (protocolver == APICMD_VER_V4)
    {
      return lte_getpsm_impl(ALTCOM_GETPSM_TYPE_NEGOTIATED,
        settings, callback);
    }

  if (sync)
    {
      presbuff = (FAR uint8_t *)&resbuff;
    }
  else
    {
      /* Setup API callback */

      ret = altcombs_setup_apicallback(APICMDID_GET_DYNAMICPSM, callback,
                                       getdynamicpsm_status_chg_cb);
      if (0 > ret)
        {
          return ret;
        }
    }

  /* Allocate API command buffer to send */

  reqbuff = (FAR uint8_t *)apicmdgw_cmd_allocbuff(cmdid,
                                                  REQ_DATA_LEN);
  if (!reqbuff)
    {
      DBGIF_LOG_ERROR("Failed to allocate command buffer.\n");
      ret = -ENOMEM;
      goto errout;
    }

  /* Send API command to modem */

  ret = apicmdgw_send(reqbuff, presbuff,
                      resbufflen, &reslen, SYS_TIMEO_FEVR);
  altcom_free_cmd(reqbuff);

  if (0 > ret)
    {
      goto errout;
    }

  ret = 0;

  if (sync)
    {
      ret = (LTE_RESULT_OK == resbuff.result) ? 0 : -EPROTO;
      if (0 == ret)
        {
          /* Parse PSM settings */

          ret = altcombs_set_psm(&resbuff.set, settings);
          if (0 > ret)
            {
              DBGIF_LOG1_ERROR("altcombs_set_psm() failed: %d\n", ret);
              ret = -EFAULT;
            }
        }
    }

  return ret;

errout:
  if (!sync)
    {
      altcombs_teardown_apicallback(APICMDID_GET_DYNAMICPSM,
                                    getdynamicpsm_status_chg_cb);
    }
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lte_get_dynamic_psm_param
 *
 * Description:
 *   Get current PSM settings.
 *
 * Input Parameters:
 *   callback Callback function to notify when getting current PSM settings
 *            is completed.
 *
 * Returned Value:
 *   On success, 0 is returned.
 *   On failure, negative value is returned according to <errno.h>.
 *
 ****************************************************************************/

int32_t lte_get_dynamic_psm_param(get_dynamic_psm_param_cb_t callback)
{
  if (!callback) {
    DBGIF_LOG_ERROR("Input argument is NULL.\n");
    return -EINVAL;
  }

  return lte_getcurrentpsm_impl(NULL, callback);
}

/****************************************************************************
 * Name: lte_get_current_psm_sync
 *
 * Description:
 *   Get current PSM settings.
 *
 * Input Parameters:
 *   settings Current PSM settings.
 *
 * Returned Value:
 *   On success, 0 is returned.
 *   On failure, negative value is returned according to <errno.h>.
 *
 ****************************************************************************/

int32_t lte_get_current_psm_sync(lte_psm_setting_t *settings)
{
  return lte_getcurrentpsm_impl(settings, NULL);
}

/****************************************************************************
 * Name: lte_get_current_psm
 *
 * Description:
 *   Get current PSM settings.
 *
 * Input Parameters:
 *   callback Callback function to notify when getting current PSM settings
 *            is completed.
 *
 * Returned Value:
 *   On success, 0 is returned.
 *   On failure, negative value is returned according to <errno.h>.
 *
 ****************************************************************************/

int32_t lte_get_current_psm(get_current_psm_cb_t callback)
{
  if (!callback) {
    DBGIF_LOG_ERROR("Input argument is NULL.\n");
    return -EINVAL;
  }

  return lte_getcurrentpsm_impl(NULL, callback);
}

/****************************************************************************
 * Name: apicmdhdlr_getdynamicpsm
 *
 * Description:
 *   This function is an API command handler for
 *   get PSM dynamic parameter result.
 *
 * Input Parameters:
 *  evt    Pointer to received event.
 *  evlen  Length of received event.
 *
 * Returned Value:
 *   If the API command ID matches APICMDID_GET_DYNAMICPSM_RES,
 *   EVTHDLRC_STARTHANDLE is returned.
 *   Otherwise it returns EVTHDLRC_UNSUPPORTEDEVENT. If an internal error is
 *   detected, EVTHDLRC_INTERNALERROR is returned.
 *
 ****************************************************************************/

enum evthdlrc_e apicmdhdlr_getdynamicpsm(FAR uint8_t *evt, uint32_t evlen)
{
  return apicmdhdlrbs_do_runjob(evt,
    APICMDID_CONVERT_RES(apicmdgw_get_cmdid(APICMDID_GET_DYNAMICPSM)),
    get_dynamicpsm_job);
}
