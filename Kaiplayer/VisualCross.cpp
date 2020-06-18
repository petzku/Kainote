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

#include "Visuals.h"
#include "KainoteMain.h"

Cross::Cross()
{

}

void Cross::OnMouseEvent(wxMouseEvent &event)
{
	if ((tab->Video->IsFullScreen() && tab->Video->GetFullScreenWindow() &&
		!tab->Video->GetFullScreenWindow()->showToolbar->GetValue()) || event.RightUp() || tab->Video->IsMenuShown()){
		if (cross){
			tab->Video->SetCursor(wxCURSOR_ARROW);
			cross = false;
			tab->Video->Render(false);
		}
		return;
	}
	
	int x = event.GetX();
	int y = event.GetY();

	if (event.Leaving()){
		if (cross){
			cross = false;
			RendererVideo *renderer = tab->Video->GetRenderer();
			tab->Video->SetCursor(wxCURSOR_ARROW); 
				
			if (tab->Video->GetState() == Paused && !renderer->m_BlockResize){ 
				tab->Video->Render(false); 
			}
		}
		return;
	}

	if (event.Entering()){
		//tab->Video->SetCursor(wxCURSOR_BLANK);
		//KaiLog(L"Cross blank");
		cross = true;
		int nx = 0, ny = 0;
		int w = 0, h = 0;
		if (tab->Video->IsFullScreen()){
			RendererVideo *renderer = tab->Video->GetRenderer();
			if (!renderer)
				return;
			diffX = renderer->m_BackBufferRect.left;
			diffY = renderer->m_BackBufferRect.top;
			w = renderer->m_BackBufferRect.right - diffX;
			h = renderer->m_BackBufferRect.bottom - diffY;
		}
		else{
			tab->Video->GetClientSize(&w, &h);
			diffX = diffY = 0;
			h -= tab->Video->GetPanelHeight();
		}
		tab->Grid->GetASSRes(&nx, &ny);
		coeffX = (float)nx / (float)(w - 1);
		coeffY = (float)ny / (float)(h - 1);
	}
	int posx = (float)x * coeffX - diffX;
	int posy = (float)y * coeffY - diffY;
	coords = L"";
	coords << posx << L", " << posy;
	DrawLines(wxPoint(x, y));

	if (event.MiddleDown() || (event.LeftDown() && event.ControlDown())){
		Dialogue *aline = tab->Edit->line;
		bool istl = (tab->Grid->hasTLMode && aline->TextTl != L"");
		wxString ltext = (istl) ? aline->TextTl : aline->Text;
		wxRegEx posmov(L"\\\\(pos|move)([^\\\\}]+)", wxRE_ADVANCED);
		posmov.ReplaceAll(&ltext, L"");

		wxString postxt;
		float posx = (float)x * coeffX - diffX;
		float posy = (float)y * coeffY - diffY;
		postxt = L"\\pos(" + getfloat(posx) + L"," + getfloat(posy) + L")";
		if (ltext.StartsWith(L"{")){
			ltext.insert(1, postxt);
		}
		else{
			ltext = L"{" + postxt + L"}" + ltext;
		}
		if (istl){ aline->TextTl = ltext; }
		else{ aline->Text = ltext; }
		tab->Grid->ChangeCell((istl) ? TXTTL : TXT, tab->Grid->currentLine, aline);
		tab->Grid->Refresh(false);
		tab->Grid->SetModified(VISUAL_POSITION);
	}
}

void Cross::Draw(int time)
{
	if (cross){
		HRESULT hr;
		DRAWOUTTEXT(font, coords, crossRect, (crossRect.left < vectors[0].x) ? 10 : 8, 0xFFFFFFFF);
		hr = line->SetWidth(3);
		hr = line->Begin();
		hr = line->Draw(&vectors[0], 2, 0xFF000000);
		hr = line->Draw(&vectors[2], 2, 0xFF000000);
		hr = line->End();
		hr = line->SetWidth(1);
		D3DXVECTOR2 v1[4];
		v1[0] = vectors[0];
		v1[0].x += 0.5f;
		v1[1] = vectors[1];
		v1[1].x += 0.5f;
		v1[2] = vectors[2];
		v1[2].y += 0.5f;
		v1[3] = vectors[3];
		v1[3].y += 0.5f;
		hr = line->Begin();
		hr = line->Draw(&v1[0], 2, 0xFFFFFFFF);
		hr = line->Draw(&v1[2], 2, 0xFFFFFFFF);
		hr = line->End();
	}
}

void Cross::DrawLines(wxPoint point)
{
	RendererVideo *renderer = tab->Video->GetRenderer();
	if (!renderer)
		return;

	wxMutexLocker lock(m_MutexCrossLines);
	

	int w, h;
	tab->Video->GetClientSize(&w, &h);
	w /= 2; h /= 2;
	crossRect.top = (h > point.y) ? point.y - 12 : point.y - 40;
	crossRect.bottom = (h > point.y) ? point.y + 23 : point.y - 5;
	crossRect.left = (w < point.x) ? point.x - 100 : point.x + 5;
	crossRect.right = (w < point.x) ? point.x - 5 : point.x + 100;

	vectors[0].x = point.x;
	vectors[0].y = 0;
	vectors[1].x = point.x;
	vectors[1].y = renderer->m_BackBufferRect.bottom;
	vectors[2].x = 0;
	vectors[2].y = point.y;
	vectors[3].x = renderer->m_BackBufferRect.right;
	vectors[3].y = point.y;
	cross = true;
	//play and pause
	if (tab->Video->GetState() <= Paused && !renderer->m_BlockResize){
		tab->Video->Render(renderer->m_VideoResized);
	}
}

void Cross::SetCurVisual()
{
	if ((tab->Video->IsFullScreen() && tab->Video->GetFullScreenWindow() && 
		!tab->Video->GetFullScreenWindow()->showToolbar->GetValue()) || tab->Video->IsMenuShown()){
		if (cross){
			tab->Video->SetCursor(wxCURSOR_ARROW);
			cross = false;
			tab->Video->Render(false);
		}
		return;
	}

	int nx = 0, ny = 0;
	int w = 0, h = 0;
	if (tab->Video->IsFullScreen()){
		RendererVideo *renderer = tab->Video->GetRenderer();
		if (!renderer)
			return;
		diffX = renderer->m_BackBufferRect.left;
		diffY = renderer->m_BackBufferRect.top;
		w = renderer->m_BackBufferRect.right - diffX;
		h = renderer->m_BackBufferRect.bottom - diffY;
	}
	else{
		tab->Video->GetClientSize(&w, &h);
		h -= tab->Video->GetPanelHeight();
		diffX = diffY = 0;
	}
	tab->Grid->GetASSRes(&nx, &ny);
	coeffX = (float)nx / (float)(w - 1);
	coeffY = (float)ny / (float)(h - 1);
}