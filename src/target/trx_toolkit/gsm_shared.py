#!/usr/bin/env python2
# -*- coding: utf-8 -*-

# TRX Toolkit
# Common GSM constants
#
# (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
#
# All Rights Reserved
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# TDMA definitions
GSM_SUPERFRAME = 26 * 51
GSM_HYPERFRAME = 2048 * GSM_SUPERFRAME

# Burst length
GSM_BURST_LEN = 148
EDGE_BURST_LEN = GSM_BURST_LEN * 3
