//  Copyright (c) 2017 - 2020, Marcin Drob

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

template<typename T>
class AbstractFFT;

class Provider;

const unsigned long line_length = 1 << 10; // number of frequency components per line (half of number of samples)
const unsigned long doublelen = line_length * 2;

class FFT
{
public:
	FFT(){};
	~FFT();
	void Set(Provider *_prov);
	void Transform(int64_t whre);
	float Get(int i);
	void SetAudio(int64_t from, int64_t to);
	float * output;
private:
	Provider *prov;
	short * input;
	AbstractFFT<float>* gfft;
	int64_t inputSize = 0;
	int64_t from = 0;
	//size_t lastend = 0;
};