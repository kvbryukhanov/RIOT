#!/usr/bin/env python3

# Copyright (C) 2017 Freie Universität Berlin
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.

import os
import sys

# It takes 35 seconds on wsn430, so add some margin
TIMEOUT = 45


def testfunc(child):
    child.expect_exact("Testing floating point arithmetics...")
    child.expect_exact("[SUCCESS]", timeout=TIMEOUT)

if __name__ == "__main__":
    sys.path.append(os.path.join(os.environ['RIOTTOOLS'], 'testrunner'))
    from testrunner import run
    sys.exit(run(testfunc))
