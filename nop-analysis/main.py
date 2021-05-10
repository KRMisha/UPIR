#!/usr/bin/env python3

import capstone
import glob
import matplotlib.pyplot as plt
import seaborn as sns
import statistics
import pandas as pd
from matplotlib.ticker import PercentFormatter
from pathlib import Path


def count_nops_in_binary(filename, arch, mode):
    with open(filename, 'rb') as f:
        md = capstone.Cs(arch, mode)
        md.skipdata = True

        code = f.read()
        instructions = md.disasm(code, 0)

        nop_count = 0
        count = 0
        for instruction in instructions:
            if instruction.mnemonic == 'nop':
                nop_count += 1
            count += 1

        return (nop_count, count)


def count_nops_for_arch(filenames, arch, mode):
    ratios = []
    for i, filename in enumerate(filenames):
        nop_count, count = count_nops_in_binary(filename, arch, mode)
        ratios.append(nop_count / count)
        print(
            f'{filename.name:<20} ({i + 1}/{len(filenames)})\t|'
            f'  NOP count: {nop_count:>6} / {count:<6} instructions  |'
            f'  Percentage: {round(ratios[-1] * 100, 2)}%'
        )

    average_ratio = statistics.mean(ratios)
    percentage = round(average_ratio * 100, 2)
    print(f'Average percentage of NOP instructions: {percentage}%')
    return average_ratio


def main():
    BINARIES_PATH = Path('binaries')
    ARCH_STRINGS = ['armhf', 'arm64', 'amd64']

    data = []

    for arch_string in ARCH_STRINGS:
        print(f'Analysis for architecture: {arch_string}')
        for program_collection in sorted(BINARIES_PATH.iterdir()):
            print(f'Program collection: {program_collection.name}')
            binaries = list(Path(program_collection / arch_string).glob('*'))

            if arch_string == 'armhf':
                arch, mode = capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM
            elif arch_string == 'arm64':
                arch, mode = capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM
            elif arch_string == 'amd64':
                arch, mode = capstone.CS_ARCH_X86, capstone.CS_MODE_64

            ratio = count_nops_for_arch(binaries, arch, mode)
            data.append({
                'arch': arch_string,
                'collection': f'{program_collection.name} (n={len(binaries)})',
                'ratio': ratio,
            })
        print()

    df = pd.DataFrame(data)

    sns.set_theme(style='whitegrid')
    g = sns.catplot(
        data=df, kind='bar',
        x='arch', y='ratio', hue='collection',
    )
    g.despine(left=True)
    g.set_axis_labels('', 'NOP instruction frequency (%)')
    for ax in g.axes.flat:
        ax.yaxis.set_major_formatter(PercentFormatter(xmax=1, decimals=2))

    plt.show()


if __name__ == '__main__':
    main()
