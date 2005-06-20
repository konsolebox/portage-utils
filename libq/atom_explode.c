/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/atom_explode.c,v 1.6 2005/06/20 23:23:57 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */

typedef struct {
	char *CATEGORY;
	char *PN;
	int PR_int;
	char *PV, *PVR;
} depend_atom;

depend_atom *atom_explode(const char *atom);
depend_atom *atom_explode(const char *atom)
{
	depend_atom *ret;
	char *ptr, *ptr_tmp;
	size_t len, slen;
	int i;
	const char *suffixes[] = { "_alpha", "_beta", "_pre", "_rc", "_p", NULL };

	/* we allocate mem for atom struct and two strings (strlen(atom)).
	 * the first string is for CAT/PN/PV while the second is for PVR */
	slen = strlen(atom);
	len = sizeof(*ret) + slen * sizeof(*atom) * 2 + 2;
	ret = (depend_atom*)xmalloc(len);
	memset(ret, 0x00, len);
	ptr = (char*)ret;
	ret->PVR = ptr + sizeof(*ret);
	ret->CATEGORY = ret->PVR + slen + 1;
	memcpy(ret->CATEGORY, atom, slen);

	/* break up the CATEOGRY and PVR */
	if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL) {
		ret->PN = ptr+1;
		*ptr = '\0';
		/* eat extra crap in case it exists */
		if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL)
			ret->CATEGORY = ptr+1;
	} else {
		ret->PN = ret->CATEGORY;
		ret->CATEGORY = NULL;
	}

	/* eat file name crap */
	if ((ptr = strstr(ret->PN, ".ebuild")) != NULL)
		*ptr = '\0';

	/* find -r# */
	ptr = ret->PN + strlen(ret->PN) - 1;
	while (*ptr && ptr > ret->PN) {
		if (!isdigit(*ptr)) {
			if (ptr[0] == 'r' && ptr[-1] == '-') {
				ret->PR_int = atoi(ptr+1);
				ptr[-1] = '\0';
			}
			break;
		}
		--ptr;
	}

	/* search for the special suffixes */
	for (i = 0; i >= 0 && suffixes[i]; ++i) {
		ptr_tmp = ret->PN;

retry_suffix:
		if ((ptr = strstr(ptr_tmp, suffixes[i])) != NULL) {
			/* check this is a real suffix and not _p hitting mod_perl */
			/* note: '_suff-' in $PN is accepted, but no one uses that ... */
			len = strlen(ptr);
			slen = strlen(suffixes[i]);
			if (slen > len) continue;
			if (ptr[slen] && !isdigit(ptr[slen]) && ptr[slen]!='-') {
				/* ok, it was a fake out ... lets skip this 
				 * fake and try to match the suffix again */
				ptr_tmp = ptr + 1;
				goto retry_suffix;
			}

eat_version:
			/* allow for 1 optional suffix letter */
			if (ptr[-1] >= 'a' && ptr[-1] <= 'z') {
				ptr_tmp = ptr--;
				while (--ptr > ret->PN)
					if (*ptr != '.' && !isdigit(*ptr))
						break;
				if (*ptr != '-') {
					ptr = ptr_tmp;
				}
			}

			/* eat the trailing version number [-.0-9]+ */
			ptr_tmp = ptr;
			while (--ptr > ret->PN)
				if (*ptr == '-') {
					ptr_tmp = ptr;
					continue;
				} else if (*ptr != '-' && *ptr != '.' && !isdigit(*ptr)) {
					ret->PV = ptr_tmp+1;
					ret->PV[-1] = '\0';
					goto found_pv;
				}
			ret->PV = ptr_tmp+1;
			*ptr_tmp = '\0';
			break;
		}
	}
	if (i <= -3)
		errf("Hrm, seem to have hit an infinite loop with %s", atom);

found_pv:
	i = -1;
	if (ret->PV) {
		sprintf(ret->PVR, "%s-r%i", ret->PV, ret->PR_int);
	} else {
		ptr = ret->PN + strlen(ret->PN);
		--i;
		goto eat_version;
	}

	return ret;
}

void atom_implode(depend_atom *atom);
void atom_implode(depend_atom *atom)
{
	if (!atom)
		errf("Atom is empty !");
	free(atom);
}
