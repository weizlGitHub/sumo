#!/usr/bin/env python
# Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
# Copyright (C) 2007-2017 German Aerospace Center (DLR) and others.
# This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v2.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v20.html

# @file    plotFlows.py
# @author  Jakob Erdmann
# @date    2017-12-01
# @version $Id$

from __future__ import absolute_import
from __future__ import print_function
import math
import sys
import os

from xml.sax import make_parser, handler
from optparse import OptionParser
from collections import defaultdict
import matplotlib.pyplot as plt

import detector
from detector import relError

SUMO_HOME = os.environ.get('SUMO_HOME',
                           os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.append(os.path.join(SUMO_HOME, 'tools'))
import sumolib  # noqa
from sumolib.xml import parse


def get_options(args=None):
    optParser = OptionParser()
    optParser.add_option("-d", "--detector-file", dest="detfile",
                         help="read detectors from FILE (mandatory)", metavar="FILE")
    optParser.add_option("-f", "--detector-flow-files", dest="flowfiles",
                         help="read detector flows to compare to from FILE (mandatory)", metavar="FILE")
    optParser.add_option("-c", "--flow-column", dest="flowcol", default="qPKW",
                         help="which column contains flows", metavar="STRING")
    optParser.add_option("-z", "--respect-zero", action="store_true", dest="respectzero",
                         default=False, help="respect detectors without data (or with permanent zero) with zero flow")
    optParser.add_option("-i", "--interval", type="int", default=60, help="aggregation interval in minutes")
    optParser.add_option("--long-names", action="store_true", dest="longnames",
                         default=False, help="do not use abbreviated names for detector groups")
    optParser.add_option("--edge-names", action="store_true", dest="edgenames",
                         default=False, help="include detector group edge name in output")
    optParser.add_option("-b", "--begin", type="float", default=0, help="begin time in minutes")
    optParser.add_option("-e", "--end", type="float", default=None, help="end time in minutes")
    optParser.add_option("-o", "--csv-output", dest="output", help="write plot data to fil", metavar="FILE")
    optParser.add_option("--extension", help="extension for saving plots", default="png")
    optParser.add_option("-s", "--show", action="store_true", default=False, help="show plot directly")
    optParser.add_option("-g", "--group-by", dest="groupby", help="group detectors (all, none, type) ", default="all")
    optParser.add_option("-t", "--type-filter", dest="typefilter", help="only show selected types")
    optParser.add_option("--id-filter", dest="idfilter", help="filter detector ids")
    optParser.add_option("--single-plot", action="store_true", dest="singleplot", default=False, help="put averything in a single plot")
    optParser.add_option("-v", "--verbose", action="store_true", default=False, help="tell me what you are doing")
    options, args = optParser.parse_args(args=args)
    if not options.detfile or not options.flowfiles:
        optParser.print_help()
        sys.exit()
    options.flowfiles = options.flowfiles.split(',')
    return options



def printFlows(options, edgeFlow, detReader):
    edgeIDCol = "edge " if options.edgenames else ""
    print('# detNames %sRouteFlow DetFlow ratio' % edgeIDCol, file=options.outfile)
    output = []
    for edge, detData in detReader._edge2DetData.iteritems():
        detString = []
        dFlow = []
        for group in detData:
            if group.isValid:
                groupName = os.path.commonprefix(group.ids)
                if groupName == "" or options.longnames:
                    groupName = ';'.join(sorted(group.ids))
                detString.append(groupName)
                dFlow.append(group.totalFlow)
        rFlow = len(detString) * [edgeFlow.get(edge, 0)]
        edges = len(detString) * [edge]
        output.extend(zip(detString, edges, rFlow, dFlow))
    for group, edge, rflow, dflow in sorted(output):
        if dflow > 0 or options.respectzero:
            if options.edgenames:
                print(group, edge, rflow, dflow, relError(rflow, dflow), file=options.outfile)
            else:
                print(group, rflow, dflow, relError(rflow, dflow), file=options.outfile)


def initDataList(begin, end, interval):
    return [None] * int(math.ceil((end - begin) / interval))

def addToDataList(data, i, val):
    if val is not None:
        if data[i] is None:
            data[i] = val
        else:
            data[i] += val



def plot(options, allData, prefix="", linestyle="-"):
    if not options.singleplot:
        fig = plt.figure()
        labelsuffix = ""
    else:
        labelsuffix = " %s" % prefix

    plt.ylabel("Avg. Simulation Flow %s" % prefix)
    plt.xlabel("Minutes")
    x = range(int(options.begin), int(options.end), options.interval)
    for f, data in zip(options.flowfiles, allData):
        plt.plot(x, data, label=f[-12:-4] + labelsuffix, linestyle=linestyle)
    plt.legend(loc='best')


def main(options):
    detReaders = [detector.DetectorReader(options.detfile, detector.LaneMap()) for f in options.flowfiles]
    for f in options.flowfiles:
        options.begin, options.end = detReaders[0].findTimes(f, options.begin, options.end)
    if options.verbose:
        print("begin=%s, end=%s" % (options.begin, options.end))
    for detReader, f in zip(detReaders, options.flowfiles):
        if options.verbose:
            print("reading %s" % f)
        detReader.clearFlows(options.begin, options.interval) # initialize
        detReader.readFlowsTimeline(f, options.interval, begin=options.begin, end=options.end, flowCol=options.flowcol)
    # aggregated detectors
    if options.singleplot:
        plt.figure(figsize=(14, 9), dpi=100)
    if options.groupby == "all":
        allData = [] # one list for each file 
        for detReader, f in zip(detReaders, options.flowfiles):
            data = initDataList(options.begin, options.end, options.interval)
            for edge, group in detReader.getGroups():
                if options.idfilter is not None and options.idfilter not in group.ids[0]:
                    continue
                assert(len(group.timeline) <= len(data))
                for i, (flow, speed) in enumerate(group.timeline):
                    addToDataList(data, i, flow)
            allData.append(data)
        plot(options, allData)
        if options.show:
            plt.show()

    elif options.groupby == "type":
        for detType, linestyle in [("source", "-"), ("sink", ":"), ("between", "--")]:
            allData = [] # one list for each file 
            for detReader, f in zip(detReaders, options.flowfiles):
                data = initDataList(options.begin, options.end, options.interval)
                for edge, group in detReader.getGroups():
                    if options.idfilter is not None and options.idfilter not in group.ids[0]:
                        continue
                    assert(len(group.timeline) <= len(data))
                    if group.type == detType :
                        for i, (flow, speed) in enumerate(group.timeline):
                            addToDataList(data, i, flow)
                allData.append(data)
            plot(options, allData, detType, linestyle)
        if options.show:
            plt.show()

    elif options.groupby == "none":
        for det, groupName in [(g.ids[0], g.getName(options.longnames)) for e, g in detReaders[0].getGroups()]:
            allData = [] # one list for each file 
            for detReader, f in zip(detReaders, options.flowfiles):
                data = initDataList(options.begin, options.end, options.interval)
                group = detReader.getGroup(det)
                if options.typefilter is not None and group.type != options.typefilter:
                    continue
                if options.idfilter is not None and options.idfilter not in group.ids[0]:
                    continue
                assert(len(group.timeline) <= len(data))
                for i, (flow, speed) in enumerate(group.timeline):
                    addToDataList(data, i, flow)
                allData.append(data)
            plot(options, allData, "%s (%s)" % (groupName, group.type))
        if options.show:
            plt.show()

    else:
        raise RuntimeError("group-by '%s' not supported" % options.groupby)


if __name__ == "__main__":
    main(get_options())
