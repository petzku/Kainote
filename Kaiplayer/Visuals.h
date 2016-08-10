#ifndef VIDEOVISUALS
#define VIDEOVISUALS
//#define UNICODE
#pragma once
#include <wx/wx.h>
#include <vector>
#include <d3d9.h>
#include <d3dx9.h>



enum{
	CHANGEPOS=1,
	MOVE,
	SCALE,
	ROTATEZ,
	ROTATEXY,
	CLIPRECT,
	VECTORCLIP,
	VECTORDRAW,
	MOVEALL
};

struct VERTEX
{	
	float fX;	
	float fY;	
	float fZ;
	D3DCOLOR Color;	
};

class TabPanel;

class ClipPoint
{
public:
	ClipPoint(int x, int y, wxString type, bool isstart);
	ClipPoint();
	bool IsInPos(wxPoint pos, int diff);
	D3DXVECTOR2 GetVector();
	int wx();
	int wy();
	int x;
	int y;
	wxString type;
	bool start;
};

class Visuals /* : public wxEvtHandler*/
{
public:
	Visuals();
	virtual ~Visuals();
	static Visuals *Get(int Visual, wxWindow *_parent);
	void SizeChanged(wxSize wsize, LPD3DXLINE _line, LPD3DXFONT _font, LPDIRECT3DDEVICE9 _device);
	void DrawRect(D3DXVECTOR2 vector);
	void DrawCircle(D3DXVECTOR2 vector);
	void DrawCross(D3DXVECTOR2 position, D3DCOLOR color = 0xFFFF0000, bool useBegin=true);
	void DrawArrow(D3DXVECTOR2 vector, D3DXVECTOR2 *vector1, int diff=0);
	
	//D3DXVECTOR2 CalcWH();
	void CreateVERTEX (VERTEX *v, float X, float Y, D3DCOLOR Color, float Z=0.0f)
	{	
		v->fX = X;	
		v->fY = Y;	
		v->fZ = Z;		
		v->Color = Color;	
	}

	void SetVisual(int _start,int _end);
	void Draw(int time);
	virtual void DrawVisual(int time){};
	virtual void SetCurVisual(){};
	virtual void OnMouseEvent(wxMouseEvent &evt){};
	virtual wxString GetVisual(){return "";};
	//D3DXVECTOR2 IsOnPoint(wxPoint pos);
	D3DXVECTOR2 CalcMovePos();
	D3DXVECTOR2 to;
	D3DXVECTOR2 lastmove;
	D3DXVECTOR2 firstmove;
	D3DXVECTOR2 from;
	
	
	double tbl[7];
	// wspw i h - potrzebne s� do przej�cia z rozdzielczo�ci napis�w i do niej
	static float wspw, wsph;

	LPD3DXLINE line;
	LPD3DXFONT font;
	LPDIRECT3DDEVICE9 device;
	wxMutex clipmutex;
	
	int start;
	int end;
	int oldtime;
	
	unsigned char Visual;
	
	wxSize SubsSize;
	wxSize VideoSize;
	TabPanel *tab;
	bool hasArrow;
	bool blockevents;
};

class Position : public Visuals
{
public:
	Position();
	//~Position();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
};

class Move : public Visuals
{
public:
	Move();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	int moveStart;
	int moveEnd;
	byte type;
	int grabbed;
	wxPoint diffs;
	wxString times;
};

struct moveElems
{
	D3DXVECTOR2 elem;
	byte type;
};

class MoveAll : public Visuals
{
public:
	MoveAll();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	void ChangeInLines(bool all);
	std::vector<moveElems> elems;
	int numElem;
	byte selectedTags;
	wxPoint diffs;
	wxPoint dumplaced;
	D3DXVECTOR2 beforeMove;
};

class Scale : public Visuals
{
public:
	Scale();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	byte type;
	int grabbed;
	byte AN;
	D3DXVECTOR2 scale;
	wxPoint diffs;
};


class RotationZ : public Visuals
{
public:
	RotationZ();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	bool isOrg;
	D3DXVECTOR2 org;
	wxPoint diffs;
};

class RotationXY : public Visuals
{
public:
	RotationXY();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	bool isOrg;
	D3DXVECTOR2 angle;
	D3DXVECTOR2 oldAngle;
	D3DXVECTOR2 org;
	byte type;
	byte AN;
	wxPoint diffs;
};

class ClipRect : public Visuals
{
public:
	ClipRect();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	wxPoint Corner[2];
	bool invClip;
	int grabbed;
	wxPoint diffs;
};

class DrawingAndClip : public Visuals
{
public:
	DrawingAndClip();
	~DrawingAndClip();
	void DrawVisual(int time);
	void OnMouseEvent(wxMouseEvent &event);
	wxString GetVisual();
	void SetCurVisual();
	void SetPos(int x, int y);
	int CheckPos(wxPoint pos, bool retlast=false, bool wsp=true);
	void MovePoint(wxPoint pos, int point);
	void AddCurve(wxPoint pos, int whereis, wxString type="b");
	void AddCurvePoint(wxPoint pos, int whereis);
	void AddLine(wxPoint pos, int whereis);
	void AddMove(wxPoint pos, int whereis);
	void DrawLine(int coord);
	void DrawRect(int coord);
	void DrawCircle(int coord);
	int DrawCurve(int i,bool bspline=false);
	void Curve(int pos, std::vector<D3DXVECTOR2> *table, bool bspline, int spoints=4, int acpt=0);

	std::vector<ClipPoint> Points;
	ClipPoint acpoint;
	bool invClip;
	bool drawtxt;
	bool newline;
	bool newmove;
	int grabbed;
	wxPoint diffs;
	D3DXVECTOR2 scale;
	// _x i _y to punkt przemieszczenia w przypadku rysunk�w.
	static float _x, _y;
};


#endif
