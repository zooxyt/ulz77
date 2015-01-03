/* Argument Parser
 * Copyright(C) 2013-2014 Cheryl Natsu

 * This file is part of multiple - Multiple Paradigm Language Interpreter

 * multiple is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * multiple is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include "argsparse.h"

int argsparse_init(int *arg_idx)
{
    *arg_idx = 1;
    return 0;
}

int argsparse_request(const int argc, const char **argv, int *arg_idx, char **p)
{
	if (*arg_idx >= argc)
	{
		return -1;
	}
	else
	{
		*p = (char *)argv[*arg_idx];
		(*arg_idx)++;
	}
	return 0;
}


