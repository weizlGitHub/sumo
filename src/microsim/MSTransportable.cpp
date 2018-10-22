/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2001-2018 German Aerospace Center (DLR) and others.
// This program and the accompanying materials
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
// SPDX-License-Identifier: EPL-2.0
/****************************************************************************/
/// @file    MSTransportable.cpp
/// @author  Melanie Weber
/// @author  Andreas Kendziorra
/// @author  Michael Behrisch
/// @date    Thu, 12 Jun 2014
/// @version $Id$
///
// The common superclass for modelling transportable objects like persons and containers
/****************************************************************************/


// ===========================================================================
// included modules
// ===========================================================================
#include <config.h>

#include <utils/common/StringTokenizer.h>
#include <utils/geom/GeomHelper.h>
#include <utils/vehicle/SUMOVehicleParameter.h>
#include <utils/vehicle/PedestrianRouter.h>
#include <utils/vehicle/IntermodalRouter.h>
#include "MSEdge.h"
#include "MSLane.h"
#include "MSNet.h"
#include <microsim/pedestrians/MSPerson.h>
#include "MSVehicleControl.h"
#include "MSTransportableControl.h"
#include "MSTransportable.h"

/* -------------------------------------------------------------------------
* static member definitions
* ----------------------------------------------------------------------- */
const double MSTransportable::ROADSIDE_OFFSET(3);


// ===========================================================================
// method definitions
// ===========================================================================
/* -------------------------------------------------------------------------
 * MSTransportable::Stage - methods
 * ----------------------------------------------------------------------- */
MSTransportable::Stage::Stage(const MSEdge* destination, MSStoppingPlace* toStop, const double arrivalPos, StageType type)
    : myDestination(destination), myDestinationStop(toStop), myArrivalPos(arrivalPos), myDeparted(-1), myArrived(-1), myType(type) {}

MSTransportable::Stage::~Stage() {}

const MSEdge*
MSTransportable::Stage::getDestination() const {
    return myDestination;
}


const MSEdge*
MSTransportable::Stage::getEdge() const {
    return myDestination;
}


const MSEdge*
MSTransportable::Stage::getFromEdge() const {
    return myDestination;
}


double
MSTransportable::Stage::getEdgePos(SUMOTime /* now */) const {
    return myArrivalPos;
}


SUMOTime
MSTransportable::Stage::getWaitingTime(SUMOTime /* now */) const {
    return 0;
}


double
MSTransportable::Stage::getSpeed() const {
    return 0.;
}


ConstMSEdgeVector
MSTransportable::Stage::getEdges() const {
    ConstMSEdgeVector result;
    result.push_back(getDestination());
    return result;
}


void
MSTransportable::Stage::setDeparted(SUMOTime now) {
    if (myDeparted < 0) {
        myDeparted = now;
    }
}

void
MSTransportable::Stage::setArrived(MSNet* /* net */, MSTransportable* /* transportable */, SUMOTime now) {
    myArrived = now;
}

bool
MSTransportable::Stage::isWaitingFor(const std::string& /*line*/) const {
    return false;
}

Position
MSTransportable::Stage::getEdgePosition(const MSEdge* e, double at, double offset) const {
    return getLanePosition(e->getLanes()[0], at, offset);
}

Position
MSTransportable::Stage::getLanePosition(const MSLane* lane, double at, double offset) const {
    return lane->getShape().positionAtOffset(lane->interpolateLanePosToGeometryPos(at), offset);
}

double
MSTransportable::Stage::getEdgeAngle(const MSEdge* e, double at) const {
    return e->getLanes()[0]->getShape().rotationAtOffset(at);
}


/* -------------------------------------------------------------------------
* MSTransportable::Stage_Trip - methods
* ----------------------------------------------------------------------- */
MSTransportable::Stage_Trip::Stage_Trip(const MSEdge* origin, const MSEdge* destination, MSStoppingPlace* toStop, const SUMOTime duration, const SVCPermissions modeSet,
    const std::string& vTypes, const double speed, const double walkFactor, const double departPosLat, const double arrivalPos) :
    MSTransportable::Stage(destination, toStop, arrivalPos, TRIP),
    myOrigin(origin),
    myDuration(duration),
    myModeSet(modeSet),
    myVTypes(vTypes),
    mySpeed(speed),
    myWalkFactor(walkFactor),
    myDepartPosLat(departPosLat) {
}


MSTransportable::Stage_Trip::~Stage_Trip() {}


Position
MSTransportable::Stage_Trip::getPosition(SUMOTime /* now */) const {
    throw ProcessError("Should not get here!");
}


double
MSTransportable::Stage_Trip::getAngle(SUMOTime /* now */) const {
    throw ProcessError("Should not get here!");
}


const MSEdge*
MSTransportable::Stage_Trip::getEdge() const {
    return myOrigin;
}


double
MSTransportable::Stage_Trip::getEdgePos(SUMOTime /* now */) const {
    return myDepartPos;
}


void
MSTransportable::Stage_Trip::setArrived(MSNet* net, MSTransportable* transportable, SUMOTime now) {
    MSTransportable::Stage::setArrived(net, transportable, now);
    MSVehicleControl& vehControl = net->getVehicleControl();
    std::vector<SUMOVehicleParameter*> pars;
    for (StringTokenizer st(myVTypes); st.hasNext();) {
        pars.push_back(new SUMOVehicleParameter());
        pars.back()->vtypeid = st.next();
        pars.back()->parametersSet |= VEHPARS_VTYPE_SET;
        pars.back()->departProcedure = DEPART_TRIGGERED;
        pars.back()->id = transportable->getID() + "_" + toString(pars.size() - 1);
    }
    if (pars.empty()) {
        if ((myModeSet & SVC_PASSENGER) != 0) {
            pars.push_back(new SUMOVehicleParameter());
            pars.back()->id = transportable->getID() + "_0";
            pars.back()->departProcedure = DEPART_TRIGGERED;
        } else if ((myModeSet & SVC_BICYCLE) != 0) {
            pars.push_back(new SUMOVehicleParameter());
            pars.back()->vtypeid = DEFAULT_BIKETYPE_ID;
            pars.back()->id = transportable->getID() + "_b0";
            pars.back()->departProcedure = DEPART_TRIGGERED;
        } else {
            // allow shortcut via busStop even when not intending to ride
            pars.push_back(nullptr);
        }
    }
    MSTransportable::Stage* previous;
    if (transportable->getNumStages() == transportable->getNumRemainingStages()) { // this is a difficult way to check that we are the first stage
        myDepartPos = transportable->getParameter().departPos;
        if (transportable->getParameter().departPosProcedure == DEPART_POS_RANDOM) {
            myDepartPos = RandHelper::rand(myOrigin->getLength());
        }
        previous = new MSTransportable::Stage_Waiting(myOrigin, -1, transportable->getParameter().depart, myDepartPos, "start", true);
    } else {
        previous = transportable->getNextStage(-1);
        myDepartPos = previous->getArrivalPos();
    }
    // TODO This works currently only for a single vehicle type
    for (SUMOVehicleParameter* vehPar : pars) {
        SUMOVehicle* vehicle = nullptr;
        if (vehPar != nullptr) {
            MSVehicleType* type = vehControl.getVType(vehPar->vtypeid);
            if (type->getVehicleClass() != SVC_IGNORING && (myOrigin->getPermissions() & type->getVehicleClass()) == 0) {
                WRITE_WARNING("Ignoring vehicle type '" + type->getID() + "' when routing person '" + transportable->getID() + "' because it is not allowed on the start edge.");
            } else {
                const MSRoute* const routeDummy = new MSRoute(vehPar->id, ConstMSEdgeVector({ myOrigin }), false, nullptr, std::vector<SUMOVehicleParameter::Stop>());
                vehicle = vehControl.buildVehicle(vehPar, routeDummy, type, !MSGlobals::gCheckRoutes);
            }
        }
        bool carUsed = false;
        std::vector<MSNet::MSIntermodalRouter::TripItem> result;
        int stageIndex = 1;
        if (net->getIntermodalRouter().compute(myOrigin, myDestination, previous->getArrivalPos(), fabs(myArrivalPos), myDestinationStop == nullptr ? "" : myDestinationStop->getID(),
            transportable->getVehicleType().getMaxSpeed() * myWalkFactor, vehicle, myModeSet, transportable->getParameter().depart, result)) {
            for (std::vector<MSNet::MSIntermodalRouter::TripItem>::iterator it = result.begin(); it != result.end(); ++it) {
                if (!it->edges.empty()) {
                    MSStoppingPlace* bs = MSNet::getInstance()->getStoppingPlace(it->destStop, SUMO_TAG_BUS_STOP);
                    double localArrivalPos = bs != nullptr ? bs->getAccessPos(it->edges.back()) : it->edges.back()->getLength() / 2.;
                    if (it + 1 == result.end() && myArrivalPos >= 0.) {
                        localArrivalPos = myArrivalPos;
                    }
                    if (it->line == "") {
                        double depPos = previous->getArrivalPos();
                        if (previous->getDestinationStop() != nullptr) {
                            depPos = previous->getDestinationStop()->getAccessPos(it->edges.front());
                        } else if (previous->getEdge() != it->edges.front()) {
//                            if (previous->getEdge()->getToJunction() == it->edges.front()->getToJunction()) {
//                                depPos = it->edges.front()->getLength();
//                            } else {
                                depPos = 0.;
//                            }
                        }
                        previous = new MSPerson::MSPersonStage_Walking(transportable->getID(), it->edges, bs, myDuration, mySpeed, depPos, localArrivalPos, myDepartPosLat);
                        transportable->appendStage(previous, stageIndex++);
                    } else if (vehicle != nullptr && it->line == vehicle->getID()) {
                        if (bs == nullptr && it + 1 != result.end()) {
                            // we have no defined endpoint and are in the middle of the trip, drive as far as possible
                            localArrivalPos = it->edges.back()->getLength();
                        }
                        previous = new MSPerson::MSPersonStage_Driving(it->edges.back(), bs, localArrivalPos, std::vector<std::string>({ it->line }));
                        transportable->appendStage(previous, stageIndex++);
                        vehicle->replaceRouteEdges(it->edges, -1, 0, "person:" + transportable->getID(), true);
                        vehicle->setArrivalPos(localArrivalPos);
                        vehControl.addVehicle(vehPar->id, vehicle);
                        carUsed = true;
                    } else {
                        previous = new MSPerson::MSPersonStage_Driving(it->edges.back(), bs, localArrivalPos, std::vector<std::string>({ it->line }), it->intended, TIME2STEPS(it->depart));
                        transportable->appendStage(previous, stageIndex++);
                    }
                }
            }
        } else {
            if (MSGlobals::gCheckRoutes) {
                const std::string error = "No connection found between '" + myOrigin->getID() + "' and '" + (myDestinationStop != nullptr ? myDestinationStop->getID() : myDestination->getID()) + "' for person '" + transportable->getID() + "'.";
                throw ProcessError(error);
            } else {
                // pedestrian will teleport
                transportable->appendStage(new MSPerson::MSPersonStage_Walking(transportable->getID(), ConstMSEdgeVector({ myOrigin, myDestination }), myDestinationStop, myDuration, mySpeed, previous->getArrivalPos(), fabs(myArrivalPos), myDepartPosLat), stageIndex++);
            }
        }
        if (vehicle != nullptr && !carUsed) {
            vehControl.deleteVehicle(vehicle, true);
        }
    }
}


void
MSTransportable::Stage_Trip::proceed(MSNet* net, MSTransportable* transportable, SUMOTime now, Stage* previous) {
    // just skip the stage, every interesting happens in setArrived
    transportable->proceed(net, now);
}


void
MSTransportable::Stage_Trip::tripInfoOutput(OutputDevice&, const MSTransportable* const) const {
}


void
MSTransportable::Stage_Trip::routeOutput(OutputDevice&, const bool /* withRouteLength */) const {
}


void
MSTransportable::Stage_Trip::beginEventOutput(const MSTransportable&, SUMOTime, OutputDevice&) const {
}


void
MSTransportable::Stage_Trip::endEventOutput(const MSTransportable&, SUMOTime, OutputDevice&) const {
}


std::string
MSTransportable::Stage_Trip::getStageSummary() const {
    return "trip from '" + myOrigin->getID() + "' to '" + getDestination()->getID() + "'";
}


/* -------------------------------------------------------------------------
* MSTransportable::Stage_Waiting - methods
* ----------------------------------------------------------------------- */
MSTransportable::Stage_Waiting::Stage_Waiting(const MSEdge* destination,
        SUMOTime duration, SUMOTime until, double pos, const std::string& actType,
        const bool initial) :
    MSTransportable::Stage(destination, nullptr, SUMOVehicleParameter::interpretEdgePos(
                               pos, destination->getLength(), SUMO_ATTR_DEPARTPOS, "stopping at " + destination->getID()),
                           initial ? WAITING_FOR_DEPART : WAITING),
    myWaitingDuration(duration),
    myWaitingUntil(until),
    myActType(actType) {
}


MSTransportable::Stage_Waiting::~Stage_Waiting() {}


SUMOTime
MSTransportable::Stage_Waiting::getUntil() const {
    return myWaitingUntil;
}


Position
MSTransportable::Stage_Waiting::getPosition(SUMOTime /* now */) const {
    return getEdgePosition(myDestination, myArrivalPos,
                           ROADSIDE_OFFSET * (MSNet::getInstance()->lefthand() ? -1 : 1));
}


double
MSTransportable::Stage_Waiting::getAngle(SUMOTime /* now */) const {
    return getEdgeAngle(myDestination, myArrivalPos) + M_PI / 2 * (MSNet::getInstance()->lefthand() ? -1 : 1);
}


void
MSTransportable::Stage_Waiting::proceed(MSNet* net, MSTransportable* transportable, SUMOTime now, Stage* previous) {
    myDeparted = now;
    const SUMOTime until = MAX3(now, now + myWaitingDuration, myWaitingUntil);
    if (dynamic_cast<MSPerson*>(transportable) != nullptr) {
        previous->getEdge()->addPerson(transportable);
        net->getPersonControl().setWaitEnd(until, transportable);
    } else {
        previous->getEdge()->addContainer(transportable);
        net->getContainerControl().setWaitEnd(until, transportable);
    }
}


void
MSTransportable::Stage_Waiting::tripInfoOutput(OutputDevice& os, const MSTransportable* const) const {
    if (myType != WAITING_FOR_DEPART) {
        os.openTag("stop");
        os.writeAttr("duration", time2string(myArrived - myDeparted));
        os.writeAttr("arrival", time2string(myArrived));
        os.writeAttr("arrivalPos", toString(myArrivalPos));
        os.writeAttr("actType", toString(myActType));
        os.closeTag();
    }
}


void
MSTransportable::Stage_Waiting::routeOutput(OutputDevice& os, const bool /* withRouteLength */) const {
    if (myType != WAITING_FOR_DEPART) {
        // lane index is arbitrary
        os.openTag("stop").writeAttr(SUMO_ATTR_LANE, getDestination()->getID() + "_0");
        if (myWaitingDuration >= 0) {
            os.writeAttr(SUMO_ATTR_DURATION, time2string(myWaitingDuration));
        }
        if (myWaitingUntil >= 0) {
            os.writeAttr(SUMO_ATTR_UNTIL, time2string(myWaitingUntil));
        }
        os.closeTag();
    }
}


void
MSTransportable::Stage_Waiting::beginEventOutput(const MSTransportable& p, SUMOTime t, OutputDevice& os) const {
    os.openTag("event").writeAttr("time", time2string(t)).writeAttr("type", "actstart " + myActType)
    .writeAttr("agent", p.getID()).writeAttr("link", getEdge()->getID()).closeTag();
}


void
MSTransportable::Stage_Waiting::endEventOutput(const MSTransportable& p, SUMOTime t, OutputDevice& os) const {
    os.openTag("event").writeAttr("time", time2string(t)).writeAttr("type", "actend " + myActType).writeAttr("agent", p.getID())
    .writeAttr("link", getEdge()->getID()).closeTag();
}


SUMOTime
MSTransportable::Stage_Waiting::getWaitingTime(SUMOTime now) const {
    return now - myDeparted;
}


void
MSTransportable::Stage_Waiting::abort(MSTransportable* t) {
    MSTransportableControl& tc = (dynamic_cast<MSPerson*>(t) != nullptr ?
                                  MSNet::getInstance()->getPersonControl() :
                                  MSNet::getInstance()->getContainerControl());
    tc.abortWaiting(t);
}


std::string
MSTransportable::Stage_Waiting::getStageSummary() const {
    std::string timeInfo;
    if (myWaitingUntil >= 0) {
        timeInfo += " until " + time2string(myWaitingUntil);
    }
    if (myWaitingDuration >= 0) {
        timeInfo += " duration " + time2string(myWaitingDuration);
    }
    return "stopping at edge '" + getDestination()->getID() + "' " + timeInfo + " (" + myActType + ")";
}


/* -------------------------------------------------------------------------
* MSTransportable::Stage_Driving - methods
* ----------------------------------------------------------------------- */
MSTransportable::Stage_Driving::Stage_Driving(const MSEdge* destination,
        MSStoppingPlace* toStop, const double arrivalPos, const std::vector<std::string>& lines,
        const std::string& intendedVeh, SUMOTime intendedDepart) :
    MSTransportable::Stage(destination, toStop, arrivalPos, DRIVING),
    myLines(lines.begin(), lines.end()),
    myVehicle(nullptr),
    myVehicleID("NULL"),
    myVehicleDistance(-1.),
    myWaitingEdge(nullptr),
    myStopWaitPos(Position::INVALID),
    myIntendedVehicleID(intendedVeh),
    myIntendedDepart(intendedDepart) {
}


MSTransportable::Stage_Driving::~Stage_Driving() {}

const MSEdge*
MSTransportable::Stage_Driving::getEdge() const {
    if (myVehicle != nullptr) {
        if (myVehicle->getLane() != nullptr) {
            return &myVehicle->getLane()->getEdge();
        }
        return myVehicle->getEdge();
    }
    return myWaitingEdge;
}


const MSEdge*
MSTransportable::Stage_Driving::getFromEdge() const {
    return myWaitingEdge;
}


double
MSTransportable::Stage_Driving::getEdgePos(SUMOTime /* now */) const {
    if (isWaiting4Vehicle()) {
        return myWaitingPos;
    }
    // vehicle may already have passed the lane (check whether this is correct)
    return MIN2(myVehicle->getPositionOnLane(), getEdge()->getLength());
}


Position
MSTransportable::Stage_Driving::getPosition(SUMOTime /* now */) const {
    if (isWaiting4Vehicle()) {
        if (myStopWaitPos != Position::INVALID) {
            return myStopWaitPos;
        }
        return getEdgePosition(myWaitingEdge, myWaitingPos,
                               ROADSIDE_OFFSET * (MSNet::getInstance()->lefthand() ? -1 : 1));
    }
    return myVehicle->getPosition();
}


double
MSTransportable::Stage_Driving::getAngle(SUMOTime /* now */) const {
    if (!isWaiting4Vehicle()) {
        MSVehicle* veh = dynamic_cast<MSVehicle*>(myVehicle);
        if (veh != nullptr) {
            return veh->getAngle();
        } else {
            return 0;
        }
    }
    return getEdgeAngle(myWaitingEdge, myWaitingPos) + M_PI / 2. * (MSNet::getInstance()->lefthand() ? -1 : 1);
}


bool
MSTransportable::Stage_Driving::isWaitingFor(const std::string& line) const {
    return myLines.count(line) > 0;
}


bool
MSTransportable::Stage_Driving::isWaiting4Vehicle() const {
    return myVehicle == nullptr;
}


SUMOTime
MSTransportable::Stage_Driving::getWaitingTime(SUMOTime now) const {
    return isWaiting4Vehicle() ? now - myDeparted : 0;
}


double
MSTransportable::Stage_Driving::getSpeed() const {
    return isWaiting4Vehicle() ? 0 : myVehicle->getSpeed();
}


ConstMSEdgeVector
MSTransportable::Stage_Driving::getEdges() const {
    ConstMSEdgeVector result;
    result.push_back(getFromEdge());
    result.push_back(getDestination());
    return result;
}

void
MSTransportable::Stage_Driving::setArrived(MSNet* net, MSTransportable* transportable, SUMOTime now) {
    MSTransportable::Stage::setArrived(net, transportable, now);
    if (myVehicle != nullptr) {
        // distance was previously set to driven distance upon embarking
        myVehicleDistance = myVehicle->getRoute().getDistanceBetween(
                                myVehicle->getDepartPos(), myVehicle->getPositionOnLane(),
                                myVehicle->getRoute().begin(),  myVehicle->getCurrentRouteEdge()) - myVehicleDistance;
    } else {
        myVehicleDistance = -1.;
    }
}

void
MSTransportable::Stage_Driving::setVehicle(SUMOVehicle* v) {
    myVehicle = v;
    myVehicleID = v->getID();
    myVehicleLine = v->getParameter().line;
    myVehicleVClass = v->getVClass();
    myVehicleDistance = myVehicle->getRoute().getDistanceBetween(
                            myVehicle->getDepartPos(), myVehicle->getPositionOnLane(),
                            myVehicle->getRoute().begin(),  myVehicle->getCurrentRouteEdge());
}

void
MSTransportable::Stage_Driving::setDestination(const MSEdge* newDestination, MSStoppingPlace* newDestStop) {
    myDestination = newDestination;
    myDestinationStop = newDestStop;
    if (newDestStop != nullptr) {
        myArrivalPos = (newDestStop->getBeginLanePosition() + newDestStop->getEndLanePosition()) / 2;
    }
}


void
MSTransportable::Stage_Driving::abort(MSTransportable* t) {
    if (myVehicle != nullptr) {
        // jumping out of a moving vehicle!
        dynamic_cast<MSVehicle*>(myVehicle)->removeTransportable(t);
    }
}


std::string
MSTransportable::Stage_Driving::getWaitingDescription() const {
    return isWaiting4Vehicle() ? ("waiting for " + joinToString(myLines, ",")
                                  + " at " + (myDestinationStop == nullptr
                                          ? ("edge '" + myWaitingEdge->getID() + "'")
                                          : ("busStop '" + myDestinationStop->getID() + "'"))
                                 ) : "";
}


void
MSTransportable::Stage_Driving::beginEventOutput(const MSTransportable& p, SUMOTime t, OutputDevice& os) const {
    os.openTag("event").writeAttr("time", time2string(t)).writeAttr("type", "arrival").writeAttr("agent", p.getID()).writeAttr("link", getEdge()->getID()).closeTag();
}


void
MSTransportable::Stage_Driving::endEventOutput(const MSTransportable& p, SUMOTime t, OutputDevice& os) const {
    os.openTag("event").writeAttr("time", time2string(t)).writeAttr("type", "arrival").writeAttr("agent", p.getID()).writeAttr("link", getEdge()->getID()).closeTag();
}



/* -------------------------------------------------------------------------
 * MSTransportable - methods
 * ----------------------------------------------------------------------- */
MSTransportable::MSTransportable(const SUMOVehicleParameter* pars, MSVehicleType* vtype, MSTransportablePlan* plan)
    : myParameter(pars), myVType(vtype), myPlan(plan) {
    myStep = myPlan->begin();
}


MSTransportable::~MSTransportable() {
    if (myPlan != nullptr) {
        for (MSTransportablePlan::const_iterator i = myPlan->begin(); i != myPlan->end(); ++i) {
            delete *i;
        }
        delete myPlan;
        myPlan = nullptr;
    }
    delete myParameter;
    if (myVType->isVehicleSpecific()) {
        MSNet::getInstance()->getVehicleControl().removeVType(myVType);
    }
}

const std::string&
MSTransportable::getID() const {
    return myParameter->id;
}

SUMOTime
MSTransportable::getDesiredDepart() const {
    return myParameter->depart;
}

void
MSTransportable::setDeparted(SUMOTime now) {
    (*myStep)->setDeparted(now);
}

double
MSTransportable::getEdgePos() const {
    return (*myStep)->getEdgePos(MSNet::getInstance()->getCurrentTimeStep());
}

Position
MSTransportable::getPosition() const {
    return (*myStep)->getPosition(MSNet::getInstance()->getCurrentTimeStep());
}

double
MSTransportable::getAngle() const {
    return (*myStep)->getAngle(MSNet::getInstance()->getCurrentTimeStep());
}

double
MSTransportable::getWaitingSeconds() const {
    return STEPS2TIME((*myStep)->getWaitingTime(MSNet::getInstance()->getCurrentTimeStep()));
}

double
MSTransportable::getSpeed() const {
    return (*myStep)->getSpeed();
}


int
MSTransportable::getNumRemainingStages() const {
    return (int)(myPlan->end() - myStep);
}

int
MSTransportable::getNumStages() const {
    return (int)myPlan->size();
}

void
MSTransportable::appendStage(Stage* stage, int next) {
    // myStep is invalidated upon modifying myPlan
    const int stepIndex = (int)(myStep - myPlan->begin());
    if (next < 0) {
        myPlan->push_back(stage);
    } else {
        if (stepIndex + next > (int)myPlan->size()) {
            throw ProcessError("invalid index '" + toString(next) + "' for inserting new stage into plan of '" + getID() + "'");
        }
        myPlan->insert(myPlan->begin() + stepIndex + next, stage);
    }
    myStep = myPlan->begin() + stepIndex;
}


void
MSTransportable::removeStage(int next) {
    assert(myStep + next < myPlan->end());
    assert(next >= 0);
    if (next > 0) {
        // myStep is invalidated upon modifying myPlan
        int stepIndex = (int)(myStep - myPlan->begin());
        delete *(myStep + next);
        myPlan->erase(myStep + next);
        myStep = myPlan->begin() + stepIndex;
    } else {
        if (myStep + 1 == myPlan->end()) {
            // stay in the simulation until the start of simStep to allow appending new stages (at the correct position)
            appendStage(new Stage_Waiting(getEdge(), 0, 0, getEdgePos(), "last stage removed", false));
        }
        (*myStep)->abort(this);
        proceed(MSNet::getInstance(), MSNet::getInstance()->getCurrentTimeStep());
    }
}


void
MSTransportable::setSpeed(double speed) {
    for (MSTransportablePlan::const_iterator i = myPlan->begin(); i != myPlan->end(); ++i) {
        (*i)->setSpeed(speed);
    }
}


void
MSTransportable::replaceVehicleType(MSVehicleType* type) {
    if (myVType->isVehicleSpecific()) {
        MSNet::getInstance()->getVehicleControl().removeVType(myVType);
    }
    myVType = type;
}


MSVehicleType&
MSTransportable::getSingularType() {
    if (myVType->isVehicleSpecific()) {
        return *myVType;
    }
    MSVehicleType* type = myVType->buildSingularType(myVType->getID() + "@" + getID());
    replaceVehicleType(type);
    return *type;
}


PositionVector
MSTransportable::getBoundingBox() const {
    PositionVector centerLine;
    const Position p = getPosition();
    const double angle = getAngle();
    const double length = getVehicleType().getLength();
    const Position back = p + Position(-cos(angle) * length, -sin(angle) * length);
    centerLine.push_back(p);
    centerLine.push_back(back);
    centerLine.move2side(0.5 * getVehicleType().getWidth());
    PositionVector result = centerLine;
    centerLine.move2side(-getVehicleType().getWidth());
    result.append(centerLine.reverse(), POSITION_EPS);
    //std::cout << " transp=" << getID() << " p=" << p << " angle=" << GeomHelper::naviDegree(angle) << " back=" << back << " result=" << result << "\n";
    return result;
}


std::string
MSTransportable::getStageSummary(int stageIndex) const {
    assert(stageIndex < (int)myPlan->size());
    assert(stageIndex >= 0);
    return (*myPlan)[stageIndex]->getStageSummary();
}


bool
MSTransportable::hasArrived() const {
    return myStep == myPlan->end();
}


void
MSTransportable::rerouteParkingArea(MSStoppingPlace* orig, MSStoppingPlace* replacement) {
    // check whether the transportable was riding to the orignal stop
    // @note: parkingArea can currently not be set as myDestinationStop so we
    // check for stops on the edge instead
    assert(getCurrentStageType() == DRIVING);
    if (getDestination() == &orig->getLane().getEdge()) {
        Stage_Driving* stage = dynamic_cast<Stage_Driving*>(*myStep);
        assert(stage != 0);
        assert(stage->getVehicle() != 0);
        // adapt plan
        stage->setDestination(&replacement->getLane().getEdge(), replacement);
        if (myStep + 1 == myPlan->end()) {
            return;
        }
        // if the next step is a walk, adapt the route
        Stage* nextStage = *(myStep + 1);
        if (nextStage->getStageType() == MOVING_WITHOUT_VEHICLE) {
            MSPerson* p = dynamic_cast<MSPerson*>(this);
            if (p != nullptr) {
                const MSEdge* to = nextStage->getDestination();
                double departPos = stage->getArrivalPos();
                double arrivalPos = nextStage->getArrivalPos();
                double speed = p->getVehicleType().getMaxSpeed();
                ConstMSEdgeVector newEdges;
                MSNet::getInstance()->getPedestrianRouter().compute(stage->getDestination(), to, departPos, arrivalPos, speed, 0, nullptr, newEdges);
                if (newEdges.empty()) {
                    WRITE_WARNING("Could not reroute person '" + getID()
                                  + "' when rerouting vehicle '" + stage->getVehicle()->getID()
                                  + "' to new parkingArea '" + replacement->getID() + "'.");
                } else {
                    //std::cout << SIMTIME << " plan before rerouting " << getID() << ":\n";
                    //for (int stage = 0; stage < p->getNumStages(); stage++) {
                    //    std::cout << stage << ": " << p->getStageSummary(stage) << "\n";;
                    //}
                    p->reroute(newEdges, departPos, 1, 2);
                    //std::cout << SIMTIME << " plan after rerouting " << getID() << ":\n";
                    //for (int stage = 0; stage < p->getNumStages(); stage++) {
                    //    std::cout << stage << ": " << p->getStageSummary(stage) << "\n";;
                    //}
                }
            }
        }
    }
}


/****************************************************************************/
