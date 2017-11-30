//  Copyright (c) 2017, Marcin Drob

//  Kainote is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  Kainote is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with Kainote.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "SubsGridBase.h"
#include <wx/window.h>

class SubsGridPreview;

class SubsGridWindow : public SubsGridBase
{
public:
	SubsGridWindow(wxWindow *parent, const long int id, const wxPoint& pos, const wxSize& size, long style);
	virtual ~SubsGridWindow();
	void AdjustWidths(int cell = 8191);
	int CalcChars(const wxString &txt, wxString *lines = NULL, bool *bad = NULL);
	void ChangeTimeDisplay(bool frame);
	void CheckText(wxString text, wxArrayInt &errs);
	void HideOverrideTags();
	void RefreshColumns(int cell = 8191);
	void RefreshIfVisible(int time);
	void ScrollTo(int y, bool center = false);
	void SelectRow(int row, bool addToSelected = false, bool select = true, bool norefresh = false);
	void SelVideoLine(int time = -1);
	void SetStyle();
	void SetVideoLineTime(wxMouseEvent &evt);
	
protected:
	void OnKeyPress(wxKeyEvent &event);
	void OnMouseEvent(wxMouseEvent &event);
	void OnPaint(wxPaintEvent& event);
	void OnScroll(wxScrollWinEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnLostCapture(wxMouseCaptureLostEvent &evt){ if (HasCapture()){ ReleaseMouse(); } holding = false; };
	void ShowSecondComparedLine(int Line, bool showPreview = false, bool fromPreview = false);

	int GridWidth[13];
	int posY=0;
	int posX=0;
	int row=0;
	int extendRow=-1;
	int lastsel=-1;
	int oldX=-1;
	bool holding=false;

	wxBitmap* bmp;
	wxFont font;
	SubsGridPreview *preview = NULL;
	SubsGridPreview *thisPreview = NULL;
private:
	virtual void ContextMenu(const wxPoint &pos, bool dummy = false){};
	//DECLARE_EVENT_TABLE();
};

