/* $Id$ */
/*-
 * Copyright (c) 2003-2004 Benedikt Meurer <benny@xfce.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <libxfce4util/libxfce4util.h>


struct _XfceKiosk
{
  gchar  *module_name;
  XfceRc *module_rc;
};


static const gchar *xfce_kiosk_lookup  (const XfceKiosk *kiosk,
                                        const gchar     *capability);
static gboolean     xfce_kiosk_chkgrp  (const gchar     *group);
static gboolean     xfce_kiosk_init    (void);


static gchar        *usrname  = NULL;
static gchar       **groups;
static const gchar  *kioskdef;
static XfceRc       *kioskrc;


/**
 * xfce_kiosk_new:
 * @module:
 *
 * FIXME
 *
 * Since: 4.2
 **/
XfceKiosk*
xfce_kiosk_new (const gchar *module)
{
  XfceKiosk *kiosk;
  gchar      path[1024];

  g_return_val_if_fail (module != NULL, NULL);
  g_return_val_if_fail (strcmp (module, "General") != 0, NULL);

  if (G_UNLIKELY (!xfce_kiosk_init ()))
    return NULL;

  g_snprintf (path, 1024, "%s/%s.kioskrc", KIOSKDIR, module);

  kiosk               = g_new (XfceKiosk, 1);
  kiosk->module_name  = g_strdup (module);
  kiosk->module_rc    = xfce_rc_simple_open (path, TRUE);

  return kiosk;
}


/**
 * xfce_kiosk_query:
 * @kiosk:
 * @capability:
 *
 * FIXME
 *
 * Return value:
 *
 * Since: 4.2
 **/
gboolean
xfce_kiosk_query (const XfceKiosk *kiosk,
                  const gchar     *capability)
{
  const gchar *value;
  gboolean     result;
  gchar      **vector;
  gchar       *string;
  gint         n;

  value = xfce_kiosk_lookup (kiosk, capability);

  /* most common case first! */
  if (G_LIKELY (value[0] == 'A'
            &&  value[1] == 'L'
            &&  value[2] == 'L'
            && (value[3] == '\0' || value[4] == ' ')))
    {
      return TRUE;
    }

  if (G_LIKELY (value[0] == 'N'
            &&  value[1] == 'O'
            &&  value[2] == 'N'
            &&  value[3] == 'E'
            && (value[4] == '\0' || value[4] == ' ')))
    {
      return FALSE;
    }

  vector = g_strsplit (value, ",", -1);
  for (n = 0, result = FALSE; vector[n] != NULL; ++n)
    {
      string = vector[n];
      if (*string == '%' && xfce_kiosk_chkgrp (string + 1))
        {
          result = TRUE;
          break;
        }
      else if (strcmp (usrname, string) == 0)
        {
          result = TRUE;
          break;
        }
    }
  g_strfreev (vector);

  return result;
}


/**
 * xfce_kiosk_free:
 * @kiosk: a valid XfceKiosk object.
 *
 * FIXME
 *
 * Since: 4.2
 */
void
xfce_kiosk_free (XfceKiosk *kiosk)
{
  if (kiosk->module_rc != NULL)
    xfce_rc_close (kiosk->module_rc);
  g_free (kiosk->module_name);
  g_free (kiosk);
}


static const gchar*
xfce_kiosk_lookup (const XfceKiosk *kiosk,
                   const gchar     *capability)
{
  const gchar *value;

  if (G_UNLIKELY (kiosk->module_rc != NULL))
    {
      value = xfce_rc_read_entry (kiosk->module_rc, capability, NULL);
      if (value != NULL)
        return value;
    }

  if (G_UNLIKELY (kioskrc != NULL))
    {
      xfce_rc_set_group (kioskrc, kiosk->module_name);
      value = xfce_rc_read_entry (kioskrc, capability, NULL);
      if (value != NULL)
        return value;
    }

  return kioskdef;
}


static gboolean
xfce_kiosk_chkgrp (const gchar *group)
{
  int n;

  for (n = 0; groups[n] != NULL; ++n)
    if (strcmp (group, groups[n]) == 0)
      return TRUE;

  return FALSE;
}


static gboolean
xfce_kiosk_init (void)
{
  struct passwd *pw;
  struct group  *gr;
  gid_t          gidset[NGROUPS_MAX];
  int            gidsetlen;
  int            n;
  int            m;

  if (G_LIKELY (usrname != NULL))
    return TRUE;

  /* determine user name */
  pw = getpwuid (getuid ());
  if (G_UNLIKELY (pw == NULL))
    return FALSE;
  usrname = g_strdup (pw->pw_name);

  /* query user groups */
  gidsetlen = getgroups (NGROUPS_MAX, gidset);
  if (G_UNLIKELY (gidsetlen < 0))
    {
      g_free (usrname); usrname = NULL;
      return FALSE;
    }
  groups = g_new (gchar *, gidsetlen + 1);
  for (n = m = 0; n < gidsetlen; ++n)
    {
      gr = getgrgid (gidset[n]);
      if (G_LIKELY (gr != NULL))
        groups[m++] = g_strdup (gr->gr_name);
    }
  groups[m] = NULL;

  /* open global $sysconfdir/xfce4/kiosk/kioskrc */
  kioskrc = xfce_rc_simple_open (KIOSKDIR "/kioskrc", TRUE);
  if (kioskrc != NULL)
    {
      xfce_rc_set_group (kioskrc, "General");
      kioskdef = xfce_rc_read_entry (kioskrc, "Default", KIOSKDEF);
    }
  else
    {
      kioskdef = KIOSKDEF;
    }

  return TRUE;
}


