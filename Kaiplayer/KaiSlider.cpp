//  Copyright (c) 2016 - 2020, Marcin Drob

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

//#include "Utils.h"
#include "KaiSlider.h"
#include <wx\bitmap.h>
#include <wx\slider.h>
#include <wx\dc.h>
#include <wx\dcclient.h>
#include <wx\dcmemory.h>
#include "config.h"
//#include <wx\log.h>

KaiSlider::KaiSlider(wxWindow *parent, int id, int _value, int _minRange, int _maxRange,
	const wxPoint& pos, const wxSize& size, long _style)
	: wxWindow(parent, id, pos, size)
	, style(_style)
	, minRange(_minRange)
	, maxRange(_maxRange)
	, thumbPos(0)
	, thumbSize(10)
	, bmp(NULL)
	, enter(false)
	, holding(false)
	, pushed(false)
	, isUpDirection(false)
{
	SetBackgroundColour(parent->GetBackgroundColour());
	wxAcceleratorEntry entries[4];
	entries[0].Set(wxACCEL_NORMAL, WXK_LEFT, ID_ACCEL_LEFT);
	entries[1].Set(wxACCEL_NORMAL, WXK_RIGHT, ID_ACCEL_RIGHT);
	entries[2].Set(wxACCEL_NORMAL, WXK_UP, ID_ACCEL_LEFT);
	entries[3].Set(wxACCEL_NORMAL, WXK_DOWN, ID_ACCEL_RIGHT);
	wxAcceleratorTable accel(4, entries);
	SetAcceleratorTable(accel);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &KaiSlider::OnArrow, this, ID_ACCEL_LEFT, ID_ACCEL_RIGHT);
	if (style & wxVERTICAL){
		SetMinSize(wxSize((size.x < 1) ? 26 : size.x, (size.y < 1) ? 150 : size.y));
	}
	else{
		SetMinSize(wxSize((size.x < 1) ? 150 : size.x, (size.y < 1) ? 26 : size.y));
	}
	if (maxRange < minRange){ maxRange = _minRange; minRange = _maxRange; }
	//value = MID(0.f, ((float)(_value - minRange)/(float)(maxRange - minRange)), 1.f);
	//if(style & wxSL_INVERSE){
	//value = 1.f - value;
	//}
	value = MID(0, _value - minRange, (maxRange - minRange));
	if (style & wxSL_INVERSE){
		value = (maxRange - minRange) - value;
	}
	pageLoop.SetOwner(this, 2345);
	Bind(wxEVT_TIMER, [=](wxTimerEvent &evt){
		if (value == 0.f || value == (maxRange - minRange)){ pageLoop.Stop(); return; }
		//int thumbRange = size - thumbSize;
		float pageSize = thumbRange / 10.f;
		if (diff <= thumbPos && isUpDirection){ thumbPos -= pageSize; }
		else if (diff >= thumbPos + thumbSize && !isUpDirection){ thumbPos += pageSize; }
		else{
			pageLoop.Stop();
			pushed = true;
			Refresh(false);
			return;
		}
		if (thumbPos < 0.f){ thumbPos = 0.f; }
		if (thumbPos > thumbRange){ thumbPos = thumbRange; }
		SendEvent();
		if (pageLoop.GetInterval() == 500){ pageLoop.Start(100); }
	}, 2345);

	Bind(wxEVT_KILL_FOCUS, [=](wxFocusEvent& evt) { Refresh(false); });
	Bind(wxEVT_SET_FOCUS, [=](wxFocusEvent& evt) { Refresh(false); });
}

void KaiSlider::OnSize(wxSizeEvent& evt)
{
	int w = 0;
	int h = 0;
	GetClientSize(&w, &h);
	int size = ((style & wxVERTICAL) != 0) ? h : w;
	thumbRange = size - thumbSize;
	valueDivide = (float)thumbRange / (float)(maxRange - minRange);
	thumbPos = (value * valueDivide) + 0.5f;
	Refresh(false);
}

void KaiSlider::OnPaint(wxPaintEvent& evt)
{
	int w = 0;
	int h = 0;
	GetClientSize(&w, &h);
	if (w == 0 || h == 0){ return; }
	wxMemoryDC tdc;
	if (bmp && (bmp->GetWidth() < w || bmp->GetHeight() < h)) {
		delete bmp;
		bmp = NULL;
	}
	if (!bmp){ bmp = new wxBitmap(w, h); }
	tdc.SelectObject(*bmp);
	tdc.SetPen(*wxTRANSPARENT_PEN);
	tdc.SetBrush(Options.GetColour(WINDOW_BACKGROUND));
	tdc.DrawRectangle(0, 0, w, h);
	bool enabled = IsThisEnabled();
	wxColour slider = (enter && !pushed) ? Options.GetColour(SLIDER_BACKGROUND_HOVER) :
		(pushed) ? Options.GetColour(SLIDER_BACKGROUND_PUSHED) :
		(enabled) ? HasFocus() ? Options.GetColour(BUTTON_BACKGROUND_ON_FOCUS) : 
		Options.GetColour(SLIDER_BACKGROUND) :
		Options.GetColour(WINDOW_BACKGROUND_INACTIVE);
	wxColour sliderBorder = (enter && !pushed) ? Options.GetColour(SLIDER_BORDER_HOVER) :
		(pushed) ? Options.GetColour(SLIDER_BORDER_PUSHED) :
		(enabled) ? HasFocus()? Options.GetColour(BUTTON_BORDER_ON_FOCUS) : 
		Options.GetColour(SLIDER_BORDER) :
		Options.GetColour(BUTTON_BORDER_INACTIVE);
	tdc.SetPen(wxPen(Options.GetColour(SLIDER_PATH_BORDER)));
	tdc.SetBrush(wxBrush(Options.GetColour(SLIDER_PATH_BACKGROUND)));
	if (style & wxVERTICAL){
		int thumbMove = (style & wxSL_INVERSE) ? 2 : 1;
		tdc.DrawRectangle((w / 2) - 2, thumbSize / 2, 4, h - thumbSize);
		tdc.SetPen(wxPen(sliderBorder));
		tdc.SetBrush(wxBrush(slider));
		tdc.DrawRectangle(2, thumbPos + 1, w - 4, thumbSize - 2);

	}
	else{
		//int thumbMove = (style & wxSL_INVERSE)? 2 : 1;
		tdc.DrawRectangle(thumbSize / 2, (h / 2) - 2, w - thumbSize, 4);
		tdc.SetPen(wxPen(sliderBorder));
		tdc.SetBrush(wxBrush(slider));
		tdc.DrawRectangle(thumbPos + 1, 2, thumbSize - 2, h - 4);

	}

	wxPaintDC dc(this);
	dc.Blit(0, 0, w, h, &tdc, 0, 0);
}

void KaiSlider::OnMouseEvent(wxMouseEvent &evt)
{
	if (evt.Leaving()){
		if (pageLoop.IsRunning()){
			pageLoop.Stop();
		}
		if (!pushed){
			enter = false;
			Refresh(false);
			return;
		}
	}

	int x = evt.GetX();
	int y = evt.GetY();
	int w = 0;
	int h = 0;
	GetClientSize(&w, &h);
	bool isVertical = ((style & wxVERTICAL) != 0);
	int coord = (isVertical) ? y : x;
	int coord2 = (isVertical) ? x : y;
	int size = (isVertical) ? h : w;
	int size2 = (isVertical) ? w : h;
	thumbRange = size - thumbSize;
	if (evt.GetWheelRotation() != 0) {
		float step = evt.GetWheelRotation() / evt.GetWheelDelta();
		thumbPos -= (step * 3);
		if (thumbPos < 0.f){ thumbPos = 0.f; }
		if (thumbPos > thumbRange){ thumbPos = thumbRange; }
		if (coord >= thumbPos && coord <= thumbPos + thumbSize){
			enter = true;
		}
		else if (enter){ enter = false; }
		SendEvent(true);
		return;
	}
	if (evt.ButtonUp()){
		if (pageLoop.IsRunning()){ pageLoop.Stop(); }
		if (HasCapture()){ ReleaseMouse(); }
		enter = false;
		pushed = false;
		holding = false;
		Refresh(false);

		wxScrollEvent evt2(wxEVT_SCROLL_THUMBRELEASE, GetId());
		evt2.SetPosition(GetValue());
		AddPendingEvent(evt2);
	}
	if (evt.RightDown() || evt.RightIsDown() || //prawy przycisk
		(evt.ShiftDown() && (evt.LeftDown() || evt.LeftIsDown()))){
		thumbPos = coord - (thumbSize / 2);
		thumbPos = MID(0, thumbPos, thumbRange);
		enter = true;
		holding = true;
		pushed = true;
		SendEvent();
		if (!HasCapture()){ CaptureMouse(); }
		return;
	}

	if (!holding){
		if (!toolTipChanged && coord >= thumbPos && coord <= thumbPos + thumbSize && coord2 >= 0 && coord2 <= size2){
			toolTipChanged = true;
			wxWindow::SetToolTip(std::to_wstring(GetValue()));
			Refresh(false);
		}
		else if (toolTipChanged && (coord < thumbPos || coord > thumbPos + thumbSize || coord2 < 0 || coord2 > size2)){
			toolTipChanged = false;
			wxWindow::SetToolTip(tip);
			Refresh(false);
		}
	}

	if (evt.LeftDown() || (!holding && evt.LeftIsDown())){

		if (coord >= thumbPos && coord <= thumbPos + thumbSize){
			pushed = true;
			holding = true;
		}
		else{
			float pageSize = thumbRange / 10.f;
			if (coord < thumbPos){ isUpDirection = true; }
			else{ isUpDirection = false; }
			thumbPos += (coord < thumbPos) ? -pageSize : pageSize;
			if (thumbPos < 0){ thumbPos = 0; }
			if (thumbPos > thumbRange){ thumbPos = thumbRange; }
			diff = coord;
			SendEvent();
			pageLoop.Start(500);
			return;
		}
		diff = coord - thumbPos;
		if (!HasCapture()){ CaptureMouse(); }
	}

	if (holding){
		thumbPos = coord - diff;
		thumbPos = MID(0, thumbPos, thumbRange);
		SendEvent();
	}
}

int KaiSlider::GetValue(){
	if (style & wxSL_INVERSE){
		int invvalue = (maxRange - minRange) - value;
		return invvalue + minRange;
	}
	else{
		return value + minRange;
	}
}

void KaiSlider::SetValue(int _value){
	value = MID(0, _value - minRange, (maxRange - minRange));
	if (style & wxSL_INVERSE){
		value = (maxRange - minRange) - value;
	}
	thumbPos = (value * valueDivide) + 0.5f;
	Refresh(false);
	Update();
}

int KaiSlider::GetThumbPosition()
{
	return thumbPos;
}

void KaiSlider::SetThumbPosition(int position)
{
	thumbPos = position;
	value = (thumbPos / valueDivide) + 0.5f;
	Refresh(false);
	Update();
}

void KaiSlider::OnArrow(wxCommandEvent& evt)
{
	int id = evt.GetId();
	int step = id == ID_ACCEL_LEFT ? -3 : 3;
	if (style & wxSL_INVERSE)
		step = -step;

	int pos = GetValue();
	pos += step;
	SetValue(pos);
	SendEvent();
}

void KaiSlider::SendEvent(bool isWheel)
{
	value = (thumbPos / valueDivide) + 0.5f;
	Refresh(false);
	Update();
	wxWindow::SetToolTip(std::to_wstring(GetValue()));
	wxScrollEvent evt2(isWheel? wxEVT_SCROLL_CHANGED : wxEVT_SCROLL_THUMBTRACK, GetId());
	evt2.SetPosition(GetValue());
	AddPendingEvent(evt2);

}

BEGIN_EVENT_TABLE(KaiSlider, wxWindow)
EVT_MOUSE_EVENTS(KaiSlider::OnMouseEvent)
EVT_PAINT(KaiSlider::OnPaint)
EVT_SIZE(KaiSlider::OnSize)
EVT_MOUSE_CAPTURE_LOST(KaiSlider::OnLostCapture)
EVT_ERASE_BACKGROUND(KaiSlider::OnEraseBackground)
END_EVENT_TABLE()