# @file
# Script to Build OVMF UEFI firmware
#
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from PlatformBuildLib import SettingsManager
from PlatformBuildLib import PlatformBuilder

    # ####################################################################################### #
    #                                Common Configuration                                     #
    # ####################################################################################### #
class CommonPlatform():
    ''' Common settings for this platform.  Define static data here and use
        for the different parts of stuart
    '''
    PackagesSupported = ("OvmfPkg",)
    ArchSupported = ("IA32", "X64")
    TargetsSupported = ("DEBUG", "RELEASE", "NOOPT")
    Scopes = ('ovmf', 'edk2-build')
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))) # MU_CHANGE - support new workspace
    PackagesPath = ("Platforms", "MU_BASECORE", "Common/MU", "Common/MU_TIANO") # MU_CHANGE add packages path

    @classmethod
    def GetDscName(cls, ArchCsv: str) -> str:
        ''' return the DSC given the architectures requested.
        ArchCsv: csv string containing all architectures to build
        '''
        dsc = "OvmfPkg"
        if "IA32" in ArchCsv.upper().split(","):
            dsc += "Ia32"
        if "X64" in ArchCsv.upper().split(","):
            dsc += "X64"
        dsc += ".dsc"
        return dsc

import PlatformBuildLib
PlatformBuildLib.CommonPlatform = CommonPlatform
