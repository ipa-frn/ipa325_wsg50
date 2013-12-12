#include "WSG50Controller.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
// #include <ros/ros.h>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

//using WSG50Observer::update;

#define DEBUG true

#ifndef MAXWIDTH
#define MAXWIDTH 110.0
#endif

#ifndef MINWIDTH
#define MINWIDTH 0.0
#endif

#ifndef MAXSPEED
#define MAXSPEED 420.0
#endif

#ifndef MINSPEED
#define MINSPEED 5.0
#endif

#ifndef MAXFORCELIMIT
#define MAXFORCELIMIT 80.0
#endif

#ifndef MINFORCELIMIT
#define MINFORCELIMIT 5.0
#endif

#ifndef MAXACCELERATION
#define MAXACCELERATION 5000.0
#endif

#ifndef MINACCELERATION
#define MINACCELERATION 100.0
#endif


bool wsg_active;


/*
 *  Constructor
 *  if this constructor is called, then the default IP and PORT will be used, which are
 *  predefined by schunk
 */
WSG50Controller::WSG50Controller(void)
{
    // set default values
    //
    this->_IP = "192.168.1.20";
    this->_PORT = "1000";

    // connect
    //
    this->setupConnection();
}

/*
 *  Destructor
 */
WSG50Controller::~WSG50Controller(void)
{
    // free memory
    //
    delete this->_wsgComm;
}


/*
 *  Constructor
 *  @param
 *      string ip = IP of the gripper
 *      string port = Port to connect with the gripper
 */
WSG50Controller::WSG50Controller(std::string ip, std::string port)
{
    this->_IP = ip;
    this->_PORT = port;

    this->setupConnection();
}

/*
 *  attach an observer to the map
 *  so it get's notified if a certain message-id is received
 */
void WSG50Controller::Attach(WSG50RosObserver * obs, unsigned int msgId)
{
    if(DEBUG) ROS_INFO("attach new RosObserver for ID: %d", msgId);


    // declare variables
    //
    std::set< WSG50RosObserver * > observerSet_;

    // check if the key is already registered in the map
    //
    if(this->_observers.find(msgId) == this->_observers.end())
    {
        // the key is not yet in the map
        //

        // generate a new set and push it into the map
        //
        observerSet_.insert(obs);
        this->_observers[msgId] = observerSet_;

        if(DEBUG) {
            ROS_INFO("observer attached! %d observers are now registerd for MsgID: %02X", (int) observerSet_.size(), msgId);
        }
    }
    else
    {
        // the key already exists
        //
        if(DEBUG) ROS_INFO("an observer is already registered for msgId: %d", msgId);

        // declare variables
        //
        std::map< unsigned int, std::set<WSG50RosObserver *> >::iterator it;
        std::set< WSG50RosObserver *>::iterator setIt;
        bool objectExists = false;


        // get the set for the required key
        //
        it = this->_observers.find(msgId);
        std::set< WSG50RosObserver *> observerSet = it->second;

        // check if the object already exists in the set
        //
        for(setIt = observerSet.begin(); setIt != observerSet.end(); ++setIt)
        {
            // compare the existing object with the new object
            //
            if(*setIt == obs) {
                objectExists = true;
                break;
            }
        }

        // add the observer into the set
        //
        if(objectExists) {
            if(DEBUG) ROS_INFO("the observer is already registered. skipp attaching.");
        }
        else
        {
            observerSet.insert(obs);
        }

        if(DEBUG) ROS_INFO("observer attached! %d observers are now registerd for MsgID: %d", (int) observerSet.size(), msgId);
    }
}

/*
 *  Detach an observer from the map, so that it doesn't get notified anymore
 */
void WSG50Controller::Detach(WSG50RosObserver *observer_, unsigned int msgId_)
{
    // declare variables
    //
    std::map< unsigned int, std::set<WSG50RosObserver *> >::iterator it;
    std::set< WSG50RosObserver *>::iterator setIt;
    bool objectExists = false;

    // check if key exists
    if(this->_observers.find(msgId_) != this->_observers.end()) {

        // get the required set
        //
        it = this->_observers.find(msgId_);
        std::set< WSG50RosObserver *> observerSet = it->second;

        // loop through set and compare the objects
        //
        for(setIt = observerSet.begin(); setIt != observerSet.end(); ++setIt)
        {
            // get the object from the current position
            WSG50RosObserver * tmpObserver_ = *setIt;

            // compare the objects
            //
            if(observer_ == tmpObserver_)
            {
                // if the objects are equal, delete this object from the set
                //
                observerSet.erase(setIt);

                // if the set is empty, delete key from map
                //
                if(observerSet.empty())
                {
                    this->_observers.erase(it);
                }
            }
        }
    }
}


void WSG50Controller::setupConnection()
{
    // initialize startup values
    //
    _checkingCommunication = false;
    _communicationOK = false;


    // initialize max and min values
    //
    this->_MaxWidth = MAXWIDTH;
    this->_MinWidth = MINWIDTH;
    this->_MaxSpeed = MAXSPEED;
    this->_MinSpeed = MINSPEED;
    this->_MaxAcceleration = MAXACCELERATION;
    this->_MinAcceleration = MINACCELERATION;
    this->_MaxForceLimit = MAXFORCELIMIT;
    this->_MinForceLimit = MINFORCELIMIT;

    // set default values
    //
    this->_acceleration = 500.0;    // mm/s²
    this->_speed        = 50.0;     // mm/s
    this->_forceLimit   = 10.0;     // N


    // get instance of the communication layer
    //
    this->_wsgComm = new WSG50Communicator(this->_IP, this->_PORT);


    // subscribe to communication layer
    //
    this->_wsgComm->Attach(this);


    // start connection
    //
    this->_wsgComm->startConnection();

    // need to wait certain time, otherwise connection won't be established.
    //
    boost::this_thread::sleep(boost::posix_time::millisec(10));

    // checking connection state
    //
    ROS_INFO("check connection...\n");
    if(this->isCommunicationOk())
    {
        ROS_INFO("Connection is up and running.\n");
    } else {
        ROS_ERROR("Connection Failure! Please try reconnecting.\n");
    }

    // gripper is ready to receive commands
    //
    this->_ready = true;
}


/*
 * UPDATE method
 * this method is inherited by the observer class and is called
 * once the subject receives an status-change, which is a response
 * message from the gripper.
 *
 * this method will overload the base-method of the observer!
 */
void WSG50Controller::update(TRESPONSE * resp)
{
    if(DEBUG) ROS_INFO("WSG50Controller::update()");

    int l, i;

    // copy id, length and status code
    _resp.id = resp->id;
    _resp.length = resp->length;
    _resp.status_code = resp->status_code;

    // assign memory
    //
    l = (int) _resp.length;
    _dat = new unsigned char[l];
    if(_resp.length > 0)
    {
        for(i=0; i<l; i++)
        {
            _dat[i] = resp->data[i];
        }
    }
    _resp.data = _dat;

    // call updateHandler in new thread
    //
    boost::thread(boost::bind(&WSG50Controller::updateHandler, this));
//    if(DEBUG) ROS_INFO("call WSG50Controller::updateHandler");
}


void WSG50Controller::updateHandler(void)
{
    if(false) {
        ROS_INFO("Controller::updateHandler(): called.");
        ROS_INFO("Controller::updateHandler(): Response Status Id: %02X", _resp.id);
        ROS_INFO("Controller::updateHandler(): Response Data Length: %d", _resp.length);
    }

    switch (_resp.id) {
    case 0x06: // _LOOP
        if(this->_checkingCommunication) {
            // we are currently checking the communication

            // check if returned data matches the sent data
            //
            bool ResultOK = true;

            if(this->_LoopTestDataLength == (int) _resp.length)
            {
                for(int i=0; i<this->_LoopTestDataLength; i++) {
                    if(this->_LoopTestData[i] != _resp.data[i]) ResultOK = false;
                }
            }
            this->_communicationOK = ResultOK;
            this->_checkingCommunication = false;
        }
        break;
    case 0x20:  // Homing
        if(_resp.status_code == E_CMD_PENDING) {
            if(DEBUG) ROS_INFO("Do homing...");
            _ready = false;
        } else if(_resp.status_code == E_SUCCESS) {
            _ready = true;
            if(DEBUG) ROS_INFO("Reached homing position.");
        } else {
            ROS_ERROR("Some error occured during homing:");
            _wsgComm->printErrorCode(_resp.status_code);
        }

        break;
    case 0x21:  // Pre-Position Fingers
        if(_resp.status_code == E_SUCCESS) {
            _ready = true;
            ROS_INFO("Reached preposition fingers position. ");
        } else if(_resp.status_code == E_AXIS_BLOCKED) {
            ROS_ERROR("Axis blocked. Stopping motion.");
            _ready = true;
        } else if(_resp.status_code == E_CMD_ABORTED) {
            ROS_WARN("STOP command has been issued while Pre-Positioning fingers.");
            _ready = true;
        } else if(_resp.status_code == E_CMD_PENDING) {
            ROS_INFO("Pre-Position Fingers: Command Pending");
            _ready = false;
        } else {
            ROS_ERROR("Pre-Position Fingers:");
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = true;
        }
        break;
    case 0x22:  // STOP Command
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("STOP command successfull");
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("STOP Command: No parameters expected!");
        } else if(_resp.status_code == E_TIMEOUT) {
            ROS_ERROR("STOP Command: timeout occured! Could not stop the gripper.");
        }
        break;
    case 0x23:  // FAST STOP Command
        if(_resp.status_code == E_SUCCESS) {
            ROS_WARN("FAST STOP has been issued successfully. Preventing any further motion until acknowledgement message.");
            _ready = false;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = false;
        }
        break;
    case 0x24:  // _ACKFASTSTOP
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("Acknowledging FAST LOCK done! Ready for next command.");
            _ready = true;
        } else if(_resp.status_code == E_CMD_FORMAT_ERROR) {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = false;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = false;
        }
        break;
    case 0x25:  // _Grasping
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("Grasped part. What to do next?");
            _ready = true;
        } else if(_resp.status_code == E_CMD_PENDING
                  || _resp.status_code == E_ALREADY_RUNNING) {
            ROS_INFO("Currently busy grasping...");
            _ready = false;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = true;
        }
        break;
    case 0x26:  // Release part
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("Part has been dropped. What to do next?");
            _ready = true;
        } else if(_resp.status_code == E_CMD_PENDING) {
            ROS_INFO("Release Part: Command Pending.");
            _ready = false;
        } else if(_resp.status_code == E_ALREADY_RUNNING) {
            ROS_ERROR("Release Part: Error already running");
            _ready = false;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = true;
        }
        break;
    case 0x30:  // _SETACC (Set Acceleration)
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("Acceleration set.");
            _ready = true;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = true;
        }
        // other status code: E_CMD_FORMAT_ERROR
        break;
    case 0x31:  // Get Acceleration
        if(_resp.status_code == E_SUCCESS) {
            memcpy(&_acceleration, _resp.data, sizeof(float)); // copy response into float acceleration variable
//            float test = 0.0;
//            memcpy(&test, _resp.data, sizeof(float));
//            printf("Acceleration Value: %f\n\n", test);
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("GET ACCELERATION response: No Parameter Expected!");
            _ready = true;
        }
        break;
    case 0x32:  // _SETFORCELIMIT
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("ForceLimit set.");
            _ready = true;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
            _ready = true;
        }
        break;
    case 0x33:  // Get Force Limit
        if(_resp.status_code == E_SUCCESS) {
            memcpy(&_forceLimit, _resp.data, sizeof(float)); // copy response into float variable
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("GET FORCE LIMIT response: No Parameter Expected!");
            _ready = true;
        }
        break;
    case 0x34:  // _SETSOFTLIMITS
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("Soft Limits have been set.");
            _ready = true;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x35:  // Get Soft Limits
        if(_resp.status_code == E_SUCCESS) {
            memcpy(&_softLimitMinus, _resp.data, sizeof(float)); // copy response into float variable
            // copy values into an temporary array
            //
            unsigned char tmp[4];
            int j;
            for(int i=0; i<4; i++) {
                j=i+4;
                tmp[i] = _resp.data[j];
            }
            memcpy(&_softLimitPlus, tmp, sizeof(float));
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("GET SOFT LIMITS response: No Parameter Expected!");
            _ready = true;
        }
        break;
    case 0x36:  // Clear Soft Limits
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("soft limits cleared successfully.");
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("CLEAR SOFT LIMITS response: No Parameter Expected!");
            _ready = true;
        } else {
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x38:  // Tare Force Sensor
        if(_resp.status_code == E_SUCCESS) {
            ROS_INFO("force sensor zeroed.");
            _ready = true;
        } else if(_resp.status_code == E_NOT_AVAILABLE) {
            ROS_ERROR("TARE FORCE SENSOR response: No force sensor installed!");
            _ready = true;
        } else if(_resp.status_code == E_ACCESS_DENIED) {
            ROS_ERROR("TARE FORCE SENSOR response: Command is not allowed in force control mode!");
            _ready = true;
        } else if(_resp.status_code == E_NO_PARAM_EXPECTED) {
            ROS_ERROR("TARE FORCE SENSOR response: No Parameter Expected!");
            _ready = true;
        } else {
            ROS_ERROR("TARE FORCE SENSOR response: Unexpected Error:");
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x40:  // Get System State
        if(_resp.status_code == E_SUCCESS) {
            // TODO
            printf("got system state!");

            _ready = true;
        } else if(_resp.status_code == E_CMD_FORMAT_ERROR) {
            ROS_ERROR("GET SYSTEM STATE response: Command length mismatch");
            _ready = true;
        } else {
            ROS_ERROR("GET SYSTEM STATE response: Unexpected Error:");
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x43:
        if(_resp.status_code == E_SUCCESS) {
            // ROS_INFO("GET OPENING WIDTH response: Success!");
            if(_resp.length == 4) {
                memcpy(&_currentOpeningWidth, _resp.data, sizeof(float));
                if(DEBUG) ROS_INFO("current opening width: %f", _currentOpeningWidth);
            } else {
                ROS_ERROR("expected 4 byte of data for opening width!");
            }
            _ready = true;
        } else if(_resp.status_code == E_CMD_FORMAT_ERROR) {
            ROS_ERROR("GET OPENING WIDTH response: Command length mismatch");
            _ready = true;
        } else {
            ROS_ERROR("GET OPENING WIDTH response: Unexpected Error:");
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x44:
        if(_resp.status_code == E_SUCCESS) {
            // ROS_INFO("GET SPEED response: Success!");
            if(_resp.length == 4) {
                memcpy(&_currentSpeed, _resp.data, sizeof(float));
                if(DEBUG) ROS_INFO("current speed: %f", _currentSpeed);
            } else {
                ROS_ERROR("expected 4 byte of data for opening width!");
            }
            _ready = true;
        } else if(_resp.status_code == E_CMD_FORMAT_ERROR) {
            ROS_ERROR("GET SPEED response: Command length mismatch");
            _ready = true;
        } else {
            ROS_ERROR("GET SPEED response: Unexpected Error:");
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    case 0x45:
        if(_resp.status_code == E_SUCCESS) {
            // ROS_INFO("GET FORCE response: Success!");
            if(_resp.length == 4) {
                memcpy(&_currentForce, _resp.data, sizeof(float));
                if(DEBUG) ROS_INFO("current force: %f", _currentForce);
            } else {
                ROS_ERROR("expected 4 byte of data for opening width!");
            }
            _ready = true;
        } else if(_resp.status_code == E_CMD_FORMAT_ERROR) {
            ROS_ERROR("GET FORCE response: Command length mismatch");
            _ready = true;
        } else {
            ROS_ERROR("GET FORCE response: Unexpected Error:");
            _wsgComm->printErrorCode(_resp.status_code);
        }
        break;
    default:
        break;
    }

    // update observer
    //
    notifyObserver((unsigned int) _resp.id, &_resp);

    // free memory
    //
    if(_resp.length > 0 && _resp.length != 0) {
        delete[] _resp.data;
        _resp.data = 0; // write zeropointer into data-pointer
    }
}

/*
 *  notify all observers, which are subscribed to this message
 */
void WSG50Controller::notifyObserver(unsigned int msgId, TRESPONSE * resp)
{
    // TODO:

    // declare variables
    //
    std::map< unsigned int, std::set<WSG50RosObserver *> >::iterator it;
    std::set< WSG50RosObserver *>::iterator setIt;
    std::set< WSG50RosObserver *> observerSet;

    if(DEBUG) ROS_WARN("notify observer for message: %02X", msgId);

    // check if there is an observer for this msgid
    //
    if(this->_observers.find(msgId) != this->_observers.end())
    {
        // get the required set of observers
        //
        it = this->_observers.find(msgId);
        observerSet = it->second;

        if(DEBUG) ROS_INFO("There are %d observers subscribed:", (int) observerSet.size());

        // loop through the set and update all observers
        //
        for(setIt = observerSet.begin(); setIt != observerSet.end(); ++setIt)
        {
            // get the observer
            //
            WSG50RosObserver * observer = *setIt;

            // send update to observer
            //
            observer->update(resp);
        }
    } // else, if there is no set of observers for this message id
    else {
        if(DEBUG) ROS_INFO("There is no observer subscribed for this message id.");
    }
}


// returns true if the gripper is currently moving
//
bool WSG50Controller::ready(void)
{
    return _ready;
}


bool WSG50Controller::isCommunicationOk()
{

    int     i,
            counter,
            millisec,
            timeoutInMillisec;
    boost::array<unsigned char, 8> data;
    bool    returnValue = false,
            runIntoTimeout = false;

    // if true, still waiting for response
    //
    _checkingCommunication = true;


    // *****************************************************************
    // Create Dummy Loop Message
    //
    _msg.id = _LOOP;

    for(i=0; i<8; i++) data[i] = 0xff;

    _msg.length = 8;
    _msg.data = data.c_array();

    // store, so that it won't be lost
    this->_LoopTestData = data.c_array();
    this->_LoopTestDataLength = _msg.length;

    this->_wsgComm->pushMessage(&_msg);


    // *****************************************************************
    // loop and check for response
    //
    // waiting for xx milisec
    millisec = 10;
    timeoutInMillisec = 2000;
    counter = 0;
    while(_checkingCommunication)
    {
        boost::this_thread::sleep(boost::posix_time::millisec(millisec));

        if((millisec * counter) >= timeoutInMillisec)
        {
            returnValue = false;
            runIntoTimeout = true;
            break;
        }
        counter++;
    }

//    printf("no longer checking Communication!\n");

    // ******************************************************************
    // Checking results
    //
    if(!runIntoTimeout && !_checkingCommunication) {
        returnValue = _communicationOK;
    } else {
        returnValue = false;
    }

    // free memory
//    delete this->_LoopTestData;
    this->_LoopTestDataLength = 0;

    return returnValue;
}


/**
 *  ####################################
 *  ###### MOTION CONTROL       ########
 *  ####################################
 *
 */
void WSG50Controller::homing()
{
    homing(0);
}

/*
 *  perform homing activity
 */
void WSG50Controller::homing(unsigned int direction)
{

    if(DEBUG) ROS_INFO("Homing...\n");

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;


    // *****************************************************************
    // Create Homing Message
    //

    TMESSAGE msg;
    boost::array<unsigned char, 1> data;

    msg.id = _HOMING;

    // 0: use default falue
    // 1: positive movement direction
    // 2: negative movement direction
    if(direction == 2)
        data[0] = 0x02;
    else if(direction == 1)
        data[0] = 0x01;
    else
        data[0] = 0x00;

    msg.length = 1;
    msg.data = data.c_array();


    // *****************************************************************
    // creating / sending message and define callback
    //
//    printf("send Homing-msg\n");


    this->_wsgComm->pushMessage(&msg);

}


/*
 * Pre-Position Fingers before graping parts
 */
void WSG50Controller::prePositionFingers(bool stopOnBlock, float width, float speed)
{
    int i, pos;

    unsigned char data[9];
    unsigned char tmp[4]; // to memcpy

    if(DEBUG) ROS_INFO("PrePositionFingers: Width = %f\n", width);

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // initialize:
    _msg.id = _PREPFINGERS; // Preposition fingers
    _msg.length = 9;

    // ***********************************************************
    // set values
    //

    // byte 0 = FLAGS
    //
    // Bit 0: Movement type
    //          1 = relative motion -> passed width is treated as offset to current opening width
    //          0 = absolute motion -> absolute width values

    // Bit 1: Stop on block, true, false

    // Bit 2-7 unused, have to be 0
    //
    if(stopOnBlock) {
        data[0] = 0x04; // this is little endian, could also be 0x40
    } else {
        data[0] = 0x00;
    }


    // byte 1-4: float width
    //
    memcpy(tmp, &width, sizeof(float));
    pos = 1;
    for(i=0; i<4; i++)
    {
        data[pos] = (unsigned char) tmp[i];
        pos++;
    }

//    printf("width: ");
//    _wsgComm->printHexArray(tmp, 4);

    // set tmp to 0
    //
    for(i=0; i<4; i++) tmp[i] = 0;

    // byte 5-8: float speed
    //
    memcpy(tmp, &speed, sizeof(float));
    pos = 5;
    for(i=0; i<4; i++)
    {
        data[pos] = (unsigned char) tmp[i];
        pos++;
    }

//    printf("speed: ");
//    _wsgComm->printHexArray(tmp, 4);

    if(DEBUG) ROS_INFO("data package for preposition fingers: ");
    _wsgComm->printHexArray(data, 9);


    // assign to message
    //
    _msg.data = data;

    // send message
    //
    this->_wsgComm->pushMessage(&_msg);
}


/*
 *  STOP
 *  this will stop any motion
 */
void WSG50Controller::stop()
{
    // Create Message
    //
    _msg.id = _STOP;
    _msg.length = 0;
    _msg.data = 0;

    // prevent further commands:
    //
    _ready = false;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}


/*
 *  FAST STOP
 *  immediately stop any motion and prevent any further motion-related commands from beeing executed
 *  Requires FAST STOP Acknowledgement message to release lock
 */
void WSG50Controller::fastStop()
{
    // Create Message
    //
    _msg.id = _FASTSTOP;
    _msg.length = 0;
    _msg.data = 0;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}

/*
 *  acknowledge previouslz issued FAST STOP or severe error
 *  to release motion-lock
 */
void WSG50Controller::ackFastStop()
{
    unsigned char data[3];

    // Create Message
    //
    _msg.id = _ACKFASTSTOP;
    _msg.length = 3;

    data[0] = 0x61;     // a
    data[1] = 0x63;     // c
    data[2] = 0x6B;     // k    = "ack"

    _msg.data = data;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}


/*
 *  Grasp Part
 *  @params:
 *      float width: in mm
 *      float speed: in mm/s
 */
void WSG50Controller::grasp(float width, float speed)
{
    int i;
    unsigned char data[8];
    unsigned char tmpFloat[4];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // check max and min values
    //
    if(width < _MinWidth) {width = _MinWidth;}
    else if(width > _MaxWidth) {width = _MaxWidth;}

    if(speed < _MinSpeed) {speed = _MinSpeed;}
    else if(speed > _MaxSpeed) {speed = _MaxSpeed;}

    // create Message
    //
    _msg.id = _GRASPPART;
    _msg.length = 8;

    // cpy float values into char array
    memcpy(tmpFloat, &width, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[i] = (unsigned char) tmpFloat[i];}

    // cpy float values into char array
    memcpy(tmpFloat, &speed, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[(i+4)] = tmpFloat[i];}

    _msg.data = data;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}


void WSG50Controller::release(float openWidth, float speed)
{
    int i;
    unsigned char data[8];
    unsigned char tmpFloat[4];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // check max and min values
    //
    if(openWidth < _MinWidth) {openWidth = _MinWidth;}
    else if(openWidth > _MaxWidth) {openWidth = _MaxWidth;}

    if(speed < _MinSpeed) {speed = _MinSpeed;}
    else if(speed > _MaxSpeed) {speed = _MaxSpeed;}

    // create Message
    //
    _msg.id = _RELEASEPART;
    _msg.length = 8;

    // cpy float values into char array
    memcpy(tmpFloat, &openWidth, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[i] = (unsigned char) tmpFloat[i];}

    // cpy float values into char array
    memcpy(tmpFloat, &speed, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[(i+4)] = tmpFloat[i];}

    _msg.data = data;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}


/**
 *  ####################################
 *  ###### MOTION CONFIGURATION ########
 *  ####################################
 *
 */

void WSG50Controller::setAcceleration(float acceleration)
{
    // declare variables
    unsigned char data[4];


    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;


    // check min/max and write to local variable
    //
    if(acceleration > this->_MaxAcceleration) {
        this->_acceleration = this->_MaxAcceleration;
    } else if(acceleration < this->_MinAcceleration) {
        this->_acceleration = this->_MinAcceleration;
    } else {
        this->_acceleration = acceleration;
    }

    // Create Message
    //

    if(DEBUG) ROS_INFO("Set Acceleration: %f\n", this->_acceleration);

    // set to moving
    //
    _ready = false;

    // initialize:
    _msg.id = _SETACC; // Set Acceleration
    _msg.length = 4;
    memcpy(data, &this->_acceleration, sizeof(float));
    _msg.data = data;

    // ***********************************************************
    // set values
    //
    this->_wsgComm->pushMessage(&_msg);
}


void WSG50Controller::setForceLimit(float forcelimit)
{
    // declare variables
    int i;
    unsigned char data[4];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;


    // check min/max and write to local variable
    //
    if(forcelimit > this->_MaxForceLimit) {
        this->_forceLimit = this->_MaxForceLimit;
    } else if(forcelimit < this->_MinForceLimit) {
        this->_forceLimit = this->_MinForceLimit;
    } else {
        this->_forceLimit = forcelimit;
    }

    // Create Message
    //

    if(DEBUG) ROS_INFO("Set ForceLimit: %f\n", this->_forceLimit);

    // set to moving
    //
    _ready = false;

    // initialize:
    _msg.id = _SETFORCELIMIT; // Preposition fingers
    _msg.length = 4;
    memcpy(data, &this->_forceLimit, sizeof(float));
    _msg.data = data;

    // ***********************************************************
    // set values
    //
    this->_wsgComm->pushMessage(&_msg);
}


/*
 *  Set operating sof limits (for grasping parts)
 *  @param:
 *      float minusLimit: prevents the fingers from moving into a certain minimum width
 *      float plusLimit: prevents the fingers from moving into a certain maximum width
 *  Measurement: mm
 */
void WSG50Controller::setSoftLimits(float minusLimit, float plusLimit)
{
    int i;
    unsigned char data[8];
    unsigned char tmpFloat[4];

    // check if ready
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // check if limits are in range
    //
    if(minusLimit < _MinWidth) minusLimit = _MinWidth;
    if(plusLimit > _MaxWidth) plusLimit = _MaxWidth;
    if(minusLimit >= _MaxWidth) {
        ROS_ERROR("minusLimit is too high!");
        return;
    }
    if(plusLimit <= _MinWidth) {
        ROS_ERROR("plusLimit is too low!");
    }

    // create Message
    //
    _msg.id = _SETSOFTLIMIT;
    _msg.length = 8;

    // cpy float values into char array
    memcpy(tmpFloat, &minusLimit, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[i] = (unsigned char) tmpFloat[i];}

    // cpy float values into char array
    memcpy(tmpFloat, &plusLimit, sizeof(float));
    // cpy into data array
    for(i=0; i<4; i++) {data[(i+4)] = tmpFloat[i];}

    _msg.data = data;

    // send message
    //
    _wsgComm->pushMessage(&_msg);
}

void WSG50Controller::clearSoftLimits()
{
    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // create message
    //
    _msg.id = _CLRSOFTLIMIT;
    _msg.length = 0;
    _msg.data = 0;

    // Send Message
    //
    this->_wsgComm->pushMessage(&_msg);

    return;
}

/*
 *  Zeroes the connected force sensor used for the force control loop
 *
 *  WARNING: this command is only allowed if not in force control mode (i.e. grasping state must not be HOLDING)
 */
void WSG50Controller::tareForceSensor()
{
    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;


    // check if state is not HOLDING
    //
    // TODO:


    // create message
    //
    _msg.id = _TAREFORCESENSOR;
    _msg.length = 0;
    _msg.data = 0;

    // send message
    //
    this->_wsgComm->pushMessage(&_msg);

    // return
    return;
}


/**
 *  ####################################
 *  ###### GETTER METHODS       ########
 *  ####################################
 *
 */
float WSG50Controller::getMaxWidth() { return this->_MaxWidth; }
float WSG50Controller::getMinWidth() { return this->_MinWidth; }
float WSG50Controller::getMaxSpeed() { return this->_MaxSpeed; }
float WSG50Controller::getMinSpeed() { return this->_MinSpeed; }
float WSG50Controller::getMaxAcceleration() { return this->_MaxAcceleration; }
float WSG50Controller::getMinAcceleration() { return this->_MinAcceleration; }
float WSG50Controller::getMaxForceLimit() { return this->_MaxForceLimit; }
float WSG50Controller::getMinForceLimit() { return this->_MinForceLimit; }



float WSG50Controller::getWidth(void)
{
    if(DEBUG) ROS_INFO("Get Width...!\n");

    // TODO:
    // check if the last update is older than XX. if so, then first get an update from the gripper
    //
    if(_widthAutoUpdate) {
        return _currentOpeningWidth;
    }

    // otherwise request width
    //
    getOpeningWidthUpdates(false, false, 1000);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return results
    //
    return _currentOpeningWidth;
}


float WSG50Controller::getSpeed(void)
{
    if(DEBUG) ROS_INFO("Get Width...!\n");

    // TODO:
    // check if the last update is older than XX. if so, then first get an update from the gripper
    //
    if(_speedAutoUpdate) {
        return _currentSpeed;
    }

    // otherwise request width
    //
    getSpeedUpdates(false, false, 1000);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return results
    //
    return _currentSpeed;
}


float WSG50Controller::getForce(void)
{
    if(DEBUG) ROS_INFO("Get Width...!\n");

    // TODO:
    // check if the last update is older than XX. if so, then first get an update from the gripper
    //
    if(_forceAutoUpdate) {
        return _currentForce;
    }

    // otherwise request width
    //
    getForceUpdates(false, false, 1000);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return results
    //
    return _currentForce;
}


void WSG50Controller::getOpeningWidthUpdates(bool updateOnChangeOnly,
                               bool automaticUpdatesEnabled,
                               short updatePeriodInMs)
{
    if(DEBUG) ROS_INFO("Set auto-updates for opening width!");

    // delete &_msg;
    unsigned char dat[3];
    unsigned char period[2];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // set Data array
    //
    memcpy(&period, &updatePeriodInMs, sizeof(short));
    dat[1] = period[0];     // remember little endian!
    dat[2] = period[1];

    // set all values to zero
    dat[0] = 0x00;
    // bit 0: autoupdate
    if(automaticUpdatesEnabled) {
        dat[0] |= 1 << 0; // bitwise OR operation. True at position 0
    } else {
        dat[0] &= ~(1 << 0);    // bitwise AND operation, negated values. Set to False at position 0
    }
    // bit 1: update on changes only, or always
    if(updateOnChangeOnly) {
        dat[0] |= 1 << 1;       // true on position 1
    } else {
        dat[0] &= ~(1 << 1);    // false on position 1
    }

    // create message
    //
    _msg.id = _GETWIDTH;
    _msg.length = 3;
    _msg.data = dat;

    // write message
    //
    _wsgComm->pushMessage(&_msg);

    // set flag for auto-update == true or false
    //
    if(automaticUpdatesEnabled) {
        _widthAutoUpdate = true;
    } else
        _widthAutoUpdate = false;
}


void WSG50Controller::getForceUpdates(bool updateOnChangeOnly,
                                      bool automaticUpdatesEnabled,
                                      short updatePeriodInMs)
{
    if(DEBUG) ROS_INFO("Set auto-updates for force!");

    // delete &_msg;
    unsigned char dat[3];
    unsigned char period[2];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // set Data array
    //
    memcpy(&period, &updatePeriodInMs, sizeof(short));
    dat[1] = period[0];     // remember little endian!
    dat[2] = period[1];

    // set all values to zero
    dat[0] = 0x00;
    // bit 0: autoupdate
    if(automaticUpdatesEnabled) {
        dat[0] |= 1 << 0; // bitwise OR operation. True at position 0
    } else {
        dat[0] &= ~(1 << 0);    // bitwise AND operation, negated values. Set to False at position 0
    }
    // bit 1: update on changes only, or always
    if(updateOnChangeOnly) {
        dat[0] |= 1 << 1;       // true on position 1
    } else {
        dat[0] &= ~(1 << 1);    // false on position 1
    }

    // create message
    //
    _msg.id = _GETFORCE;
    _msg.length = 3;
    _msg.data = dat;

    // write message
    //
    _wsgComm->pushMessage(&_msg);

    // set flag for auto-update == true or false
    //
    if(automaticUpdatesEnabled) {
        _forceAutoUpdate = true;
    } else
        _forceAutoUpdate = false;
}


void WSG50Controller::getSpeedUpdates(bool updateOnChangeOnly,
                                      bool automaticUpdatesEnabled,
                                      short updatePeriodInMs)
{
    if(DEBUG) ROS_INFO("Set auto-updates for speed!");

    // delete &_msg;
    unsigned char dat[3];
    unsigned char period[2];

    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // set Data array
    //
    memcpy(&period, &updatePeriodInMs, sizeof(short));
    dat[1] = period[0];     // remember little endian!
    dat[2] = period[1];

    // set all values to zero
    dat[0] = 0x00;
    // bit 0: autoupdate
    if(automaticUpdatesEnabled) {
        dat[0] |= 1 << 0; // bitwise OR operation. True at position 0
    } else {
        dat[0] &= ~(1 << 0);    // bitwise AND operation, negated values. Set to False at position 0
    }
    // bit 1: update on changes only, or always
    if(updateOnChangeOnly) {
        dat[0] |= 1 << 1;       // true on position 1
    } else {
        dat[0] &= ~(1 << 1);    // false on position 1
    }

    // create message
    //
    _msg.id = _GETSPEED;
    _msg.length = 3;
    _msg.data = dat;

    // write message
    //
    _wsgComm->pushMessage(&_msg);

    // set flag for auto-update == true or false
    //
    if(automaticUpdatesEnabled) {
        _speedAutoUpdate = true;
    } else
        _speedAutoUpdate = false;
}


// not asynchroneous
//
void WSG50Controller::getSoftLimits(float *softLimits)
{
    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // Create Message
    //
    _msg.id = _GETSOFTLIMIT;  // Get Acceleration
    _msg.length = 0;
    _msg.data = 0;

    // Send Message
    //
    this->_wsgComm->pushMessage(&_msg);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // set values
    //
    softLimits[0] = _softLimitMinus;
    softLimits[1] = _softLimitPlus;

    // return Acceleration
    //
    return;
}


// not asynchroneous
//
float WSG50Controller::getForceLimit()
{
    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return _forceLimit;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // Create Message
    //
    _msg.id = _GETFORCELIMIT;  // Get Acceleration
    _msg.length = 0;
    _msg.data = 0;

    // Send Message
    //
    this->_wsgComm->pushMessage(&_msg);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return Acceleration
    //
    return _forceLimit;
}


// not asynchron.
float WSG50Controller::getAcceleration()
{
    // check if ready
    //
    if(!_ready) {
        ROS_ERROR("Gripper is not ready to receive another command!");
        return _acceleration;
    }
    // prevent from sending other messages
    //
    _ready = false;

    // Create Message
    //
    _msg.id = _GETACC;  // Get Acceleration
    _msg.length = 0;
    _msg.data = 0;

    // Send Message
    //
    this->_wsgComm->pushMessage(&_msg);

    // Loop Wait for Response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return Acceleration
    //
    return _acceleration;
}


SSTATE WSG50Controller::getSystemState(bool updateOnChangeOnly,
                                       bool enableAutoUpdate,
                                       short updatePeriodInMillisec)
{
    // variable instantiation
    //
    unsigned char dat[3];
    unsigned char period[2];

    // create data package
    //
    // set all values to zero
    dat[0] = 0x00;
    // bit 0: autoupdate
    if(enableAutoUpdate) {
        dat[0] |= 1 << 0; // bitwise OR operation. True at position 0
    } else {
        dat[0] &= ~(1 << 0);    // bitwise AND operation, negated values. Set to False at position 0
    }
    // bit 1: update on changes only, or always
    if(updateOnChangeOnly) {
        dat[0] |= 1 << 1;       // true on position 1
    } else {
        dat[0] &= ~(1 << 1);    // false on position 1
    }

//    // update period
//    memcpy(&period, updatePeriodInMillisec, sizeof(short));
//    dat[1] = period[0];
//    dat[2] = period[1];


    // create message
    //
    _msg.id == _GETSYSTSTATE;
    _msg.length = 3;
    _msg.data = dat;

    // send message
    //
    this->_wsgComm->pushMessage(&_msg);

    // wait for response
    //
    while(!_ready) boost::this_thread::sleep(boost::posix_time::millisec(20));

    // return state
    //
    return _systemState;
}



int WSG50Controller::getGraspingState(bool updateOnChangeOnly,
                                      bool enableAutoUpdate,
                                      short updatePeriodInMillisec)
{


    return 1;
}
