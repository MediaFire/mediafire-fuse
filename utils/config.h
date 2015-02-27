/*
 * Copyright (C) 2014, 2015
 * Johannes Schauer <j.schauer@email.de>
 * Bryan Christ <bryan.christ@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/*
   find the mediafire-tools configuration file.  if it doesn't exist
   create one.
*/
void config_file_init(char **configfile);

/*
   load settings from the specified configuration file and push the
   arguments from the file into argc and argv for a parser.
   the return value indicates the number of new items found.  if the value
   is positive, then argv will have beenbe relloc'd and null terminated.
*/
int config_file_read(FILE *fp,int *argc, char ***argv);

#endif
