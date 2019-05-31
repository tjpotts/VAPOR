//************************************************************************
//															*
//		     Copyright (C)  2006										*
//     University Corporation for Atmospheric Research					*
//		     All Rights Reserved										*
//															*
//************************************************************************/
//
//	File:		EventRouter.cpp
//
//	Author:		Alan Norton
//			National Center for Atmospheric Research
//			PO 3000, Boulder, Colorado
//
//	Date:		October 2006
//
//	Description:	Implements the (pure virtual) EventRouter class.
//		This class supports routing messages from the gui to the params
//
#ifdef WIN32
    // Annoying unreferenced formal parameter warning
    #pragma warning(disable : 4100)
#endif
#include <vapor/glutil.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <qurl.h>
#include <QObject>
#include <qapplication.h>
#include <qcursor.h>
#include <qmessagebox.h>
#include <qfileinfo.h>
#include <QFileDialog>
#include <qgroupbox.h>
#include <qradiobutton.h>
#include <vapor/ControlExecutive.h>
#include <vapor/ParamsMgr.h>
#include <vapor/DataMgr.h>
#include "qbuttongroup.h"
#include "EventRouter.h"

using namespace VAPoR;

EventRouter::EventRouter(ControlExec *ce, string paramsType)
{
    VAssert(ce != NULL);

    _controlExec = ce;
    _paramsType = paramsType;
    _textChangedFlag = false;
}

ParamsBase *EventRouter::GetActiveParams() const
{
    ParamsMgr *paramsMgr = _controlExec->GetParamsMgr();
    VAssert(paramsMgr);

    return (paramsMgr->GetParams(_paramsType));
}

void EventRouter::StartCursorMove()
{
    confirmText();
    SetTextChanged(false);
}
void EventRouter::EndCursorMove()
{
    // Update the tab, it's in front:
    updateTab();
}

void EventRouter::updateTab()
{
    // Obtain the Params instance that is currently active.
    ParamsBase *myParams = GetActiveParams();

    // If the Params is not valid do not proceed.
    if (!myParams) return;

    // Ugh. Use the path name to a ParamsBase instance to see if it
    // has been initialized. Can't use it's address because it will
    // change after undo/redo or session save/restore :-(
    //
    string path = myParams->GetNode()->GetPath();
    if (find(_initPaths.begin(), _initPaths.end(), path) == _initPaths.end()) {
        _initializeTab();
        _initPaths.push_back(path);
    }

    _updateTab();

    // Turn off the textChanged flag, to ignore any signals that were generated by this method.
    SetTextChanged(false);
}

// Whenever the user presses enter, call confirmText
// This method takes all the values in the gui and sets them into the Params.
void EventRouter::confirmText()
{
    // If the textChanged flag has not been set, there's nothing to do.
    if (!_textChangedFlag) return;

    // Obtain the active params
    ParamsBase *myParams = GetActiveParams();
    if (!myParams) return;

#ifdef VAPOR3_0_0_ALPHA
    // Capture all the changes in one Undo/Redo queue event:
    Command *cmd = Command::CaptureStart(myParams, "tab text edit");
#endif

    // Call method on subclass to respond to text changes
    _confirmText();

    // Turn off the textChanged flag; we have processed all the text changes.
    SetTextChanged(false);

#ifdef VAPOR3_0_0_ALPHA
    // Call Validate(2) to force all the values in the Params to legitimate values.
    myParams->Validate(2);

    // All the changes that were made are captured with Command::CaptureEnd
    Command::CaptureEnd(cmd, myParams);
#endif

    // We may need to redisplay the tab (e.g. if Validate() resulted in a change).
    updateTab();
}
