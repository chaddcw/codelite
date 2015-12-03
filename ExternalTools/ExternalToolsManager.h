//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// Copyright            : (C) 2015 The CodeLite Team
// File name            : ExternalToolsManager.h
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

#ifndef EXTERNALTOOLSMANAGER_H
#define EXTERNALTOOLSMANAGER_H
#include "external_tools.h"

class ExternalToolsManager : public ExternalToolsManagerBase
{
public:
    ExternalToolsManager(wxWindow* parent);
    virtual ~ExternalToolsManager();

protected:
    virtual void OnRefresh(wxCommandEvent& event);
    void DoPopulateTable();
    void DoClear();

protected:
    virtual void OnKill(wxCommandEvent& event);
    virtual void OnKillAll(wxCommandEvent& event);
    virtual void OnKillAllUI(wxUpdateUIEvent& event);
    virtual void OnKillUI(wxUpdateUIEvent& event);
};
#endif // EXTERNALTOOLSMANAGER_H