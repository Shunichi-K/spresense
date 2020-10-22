/****************************************************************************
 * modules/lte/altcom/api/mbedtls/md_digest.c
 *
 *   Copyright 2018 Sony Corporation
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

#include <string.h>
#include "dbg_if.h"
#include "altcom_errno.h"
#include "altcom_seterrno.h"
#include "apicmd_md_digest.h"
#include "apiutil.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/md.h"
#include "mbedtls/md_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MD_DIGEST_REQ_DATALEN (sizeof(struct apicmd_md_digest_s))
#define MD_DIGEST_RES_DATALEN (sizeof(struct apicmd_md_digestres_s))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct md_digest_req_s
{
  uint32_t md_id;
  uint32_t chain_id;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int32_t md_digest_request(FAR struct md_digest_req_s *req,
                                 unsigned char *output)
{
  int32_t                          ret;
  uint16_t                         reslen = 0;
  FAR struct apicmd_md_digest_s    *cmd = NULL;
  FAR struct apicmd_md_digestres_s *res = NULL;

  /* Allocate send and response command buffer */

  if (!altcom_mbedtls_alloc_cmdandresbuff(
    (FAR void **)&cmd, APICMDID_TLS_MD_DIGEST, MD_DIGEST_REQ_DATALEN,
    (FAR void **)&res, MD_DIGEST_RES_DATALEN))
    {
      return MBEDTLS_ERR_MD_ALLOC_FAILED;
    }

  /* Fill the data */

  cmd->md_info = htonl(req->md_id);
  cmd->chain = htonl(req->chain_id);

  DBGIF_LOG1_DEBUG("[md_digest]md_info id: %d\n", req->md_id);
  DBGIF_LOG1_DEBUG("[md_digest]chain id: %d\n", req->chain_id);

  /* Send command and block until receive a response */

  ret = apicmdgw_send((FAR uint8_t *)cmd, (FAR uint8_t *)res,
                      MD_DIGEST_RES_DATALEN, &reslen,
                      SYS_TIMEO_FEVR);

  if (ret < 0)
    {
      DBGIF_LOG1_ERROR("apicmdgw_send error: %d\n", ret);
      goto errout_with_cmdfree;
    }

  if (reslen != MD_DIGEST_RES_DATALEN)
    {
      DBGIF_LOG1_ERROR("Unexpected response data length: %d\n", reslen);
      ret = MBEDTLS_ERR_MD_FILE_IO_ERROR;
      goto errout_with_cmdfree;
    }

  ret = ntohl(res->ret_code);
  memcpy(output, res->output, APICMD_MD_DIGEST_OUTPUT_LEN);

  DBGIF_LOG1_DEBUG("[md_digest res]ret: %d\n", ret);

  altcom_mbedtls_free_cmdandresbuff(cmd, res);

  return ret;

errout_with_cmdfree:
  altcom_mbedtls_free_cmdandresbuff(cmd, res);
  return ret;
}



/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mbedtls_md_digest(const mbedtls_md_info_t *md_info, mbedtls_x509_crt *chain,
                      unsigned char *output)
{
  struct md_digest_req_s req;

  if (!altcom_isinit())
    {
      DBGIF_LOG_ERROR("Not intialized\n");
      altcom_seterrno(ALTCOM_ENETDOWN);
      return 0;
    }

  req.md_id = md_info->id;
  req.chain_id = chain->id;

  return md_digest_request(&req, output);
}

