#!/usr/bin/env python3
"""uefiplat.cfg (stock Qualcomm XBL UEFI) -> Mu-Silicium MemoryMapLib.c and ConfigurationMapLib.c.
  cfg2maplib.py uefiplat.cfg OUT_LIBRARY_DIR"""
import os, re, sys

cfg, out = sys.argv[1], sys.argv[2]
section = None
mem, regs, conf = [], [], []
for raw in open(cfg, encoding='latin-1'):
    line = raw.split('#', 1)[0].strip()
    if not line:
        continue
    m = re.match(r'^\[(\w+)\]$', line)
    if m:
        section = m.group(1)
        continue
    if section in ('MemoryMap', 'RegisterMap'):
        f = [x.strip() for x in re.split(r',(?=(?:[^"]*"[^"]*")*[^"]*$)', line)]
        if len(f) != 8:
            continue
        row = '  {%-20s %s, %s, %s, %s, %s, %s, %s},' % (f[2] + ',', f[0], f[1], f[3], f[4], f[5], f[6], f[7])
        (mem if section == 'MemoryMap' else regs).append(row)
    elif section == 'ConfigParameters':
        m = re.match(r'^(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)$', line)
        if m:
            conf.append('  {"%s", %s},' % (m.group(1), m.group(2)))
os.makedirs(os.path.join(out, 'MemoryMapLib'), exist_ok=True)
os.makedirs(os.path.join(out, 'ConfigurationMapLib'), exist_ok=True)
body = ['#include <Library/MemoryMapLib.h>', '', 'STATIC', 'EFI_MEMORY_REGION_DESCRIPTOR', 'gMemoryDescriptor[] = {',
        '  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute', '',
        '  // DDR Regions (stock penang uefiplat.cfg)'] + mem + ['', '  // Register Regions'] + regs
body[-1] = body[-1].rstrip(',')
body += ['};', '', 'VOID', 'GetMemoryMap (', '  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,',
         '  OUT UINT8                         *MemoryDescriptorCount)', '{', '  // Pass Data',
         '  *MemoryDescriptor      = gMemoryDescriptor;', '  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);', '}', '']
open(os.path.join(out, 'MemoryMapLib', 'MemoryMapLib.c'), 'w').write('\n'.join(body))
cb = ['#include <Library/ConfigurationMapLib.h>', '', 'STATIC', 'EFI_CONFIGURATION_ENTRY_DESCRIPTOR',
      'gConfigurationDescriptor[] = {', '  // Configuration Map (stock penang uefiplat.cfg)'] + conf + ['};', '', 'VOID', 'GetConfigurationMap (', '  OUT EFI_CONFIGURATION_ENTRY_DESCRIPTOR **ConfigurationDescriptor,', '  OUT UINT8                               *ConfigurationDescriptorCount)', '{', '  // Pass Data', '  *ConfigurationDescriptor      = gConfigurationDescriptor;', '  *ConfigurationDescriptorCount = ARRAY_SIZE (gConfigurationDescriptor);', '}']
open(os.path.join(out, 'ConfigurationMapLib', 'ConfigurationMapLib.c'), 'w').write('\n'.join(cb) + '\n')
print('memory %d, registers %d, config %d' % (len(mem), len(regs), len(conf)))
