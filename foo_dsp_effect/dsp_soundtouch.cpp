#define _WIN32_WINNT 0x0501
#include "../SDK/foobar2000.h"
#include "../ATLHelpers/ATLHelpers.h"
#include "resource.h"
#include "SoundTouch/SoundTouch.h"
#include "circular_buffer.h"
using namespace soundtouch;

static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupRate( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
static void RunDSPConfigPopupTempo( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback );
#define BUFFER_SIZE 2048

class dsp_pitch : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<soundtouch::SAMPLETYPE>sample_buffer;
	pfc::array_t<soundtouch::SAMPLETYPE>samplebuf;
	unsigned buffered;
	bool st_enabled;
private:
	void insert_chunks()
	{
			uint samples = p_soundtouch->numSamples();
			if (!samples) return;
			samplebuf.grow_size(BUFFER_SIZE * m_ch);
			soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
			do
			{
				samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
				if (samples > 0)
				{
					audio_chunk * chunk = insert_chunk(samples * m_ch);
					chunk->set_channels(m_ch,m_ch_mask);
					chunk->set_data_32(src, samples, m_ch, m_rate);
				}
			}
			while (samples != 0);
	}


public:
	dsp_pitch( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		buffered=0;
		p_soundtouch = 0;
		parse_preset( pitch_amount, in );
		st_enabled = true;
	}
	~dsp_pitch(){
		if (p_soundtouch) 
		delete p_soundtouch;
		p_soundtouch = 0;
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// Create these with guidgen.exe.
		// {A7FBA855-56D4-46AC-8116-8B2A8DF2FB34}
		static const GUID guid =
		{ 0xa7fba855, 0x56d4, 0x46ac, { 0x81, 0x16, 0x8b, 0x2a, 0x8d, 0xf2, 0xfb, 0x34 } };
		return guid;
	}

	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Pitch Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
	}

	virtual void on_endofplayback(abort_callback & p_abort) {
        //same as flush, only at end of playback
		if (p_soundtouch && st_enabled)
		{
				insert_chunks();
				if (buffered)
				{
					sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
					p_soundtouch->putSamples(samplebuf.get_ptr(),buffered);
					buffered = 0;
				}
				p_soundtouch->flush();
				insert_chunks();	
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			p_soundtouch = new SoundTouch;
			if (!p_soundtouch) return 0;
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			p_soundtouch->setSampleRate(m_rate);
			p_soundtouch->setChannels(m_ch);
			p_soundtouch->setPitchSemiTones(pitch_amount);
			st_enabled = true;
			if (pitch_amount== 0)st_enabled = false;
			bool usequickseek = false;
			bool useaafilter = false; //seems clearer without it
			p_soundtouch->setSetting(SETTING_USE_QUICKSEEK, usequickseek);
			p_soundtouch->setSetting(SETTING_USE_AA_FILTER, useaafilter);
		}
		samplebuf.grow_size(BUFFER_SIZE*m_ch);

		if (!st_enabled) return true;

		while (sample_count > 0)
		{    
			int todo = min(BUFFER_SIZE - buffered, sample_count);
			sample_buffer.write(src,todo*m_ch);
			src += todo * m_ch;
			buffered += todo;
			sample_count -= todo;
			if (buffered == BUFFER_SIZE)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
				insert_chunks();
				buffered = 0;
			}
		}
		return false;
	}

	virtual void flush() {
		if (p_soundtouch){
			p_soundtouch->clear();
		}
		m_rate = 0;
		m_ch = 0;
		buffered = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		return (p_soundtouch && m_rate && st_enabled) ? ((double)(p_soundtouch->numSamples() + buffered) / (double)m_rate) : 0;
	}


	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopup( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
		}
		catch(exception_io_data) {pitch = 0.0;}
	}
};

class dsp_tempo : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<soundtouch::SAMPLETYPE>sample_buffer;
	pfc::array_t<soundtouch::SAMPLETYPE>samplebuf;
	unsigned buffered;
	bool st_enabled;
private:
	void insert_chunks()
	{
		uint samples = p_soundtouch->numSamples();
		if (!samples) return;
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
		do
		{
			samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
			if (samples > 0)
			{
				audio_chunk * chunk = insert_chunk(samples * m_ch);
				//	chunk->set_channels(m_ch,m_ch_mask);
				chunk->set_data_32(src, samples, m_ch, m_rate);
			}
		}
		while (samples != 0);
	}

public:
	dsp_tempo( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		p_soundtouch=0;
		buffered=0;
		parse_preset( pitch_amount, in );
		st_enabled = true;
	}
	~dsp_tempo(){
		if (p_soundtouch) delete p_soundtouch;
		p_soundtouch = 0;
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {44BCACA2-9EDD-493A-BB8F-9474F4B5A76B}
		static const GUID guid = 
		{ 0x44bcaca2, 0x9edd, 0x493a, { 0xbb, 0x8f, 0x94, 0x74, 0xf4, 0xb5, 0xa7, 0x6b } };
		return guid;
	}

	// We also need a name, so the user can identify the DSP.
	// The name we use here does not describe what the DSP does,
	// so it would be a bad name. We can excuse this, because it
	// doesn't do anything useful anyway.
	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Tempo Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		// This method is called when a track ends.
		// We need to do the same thing as flush(), so we just call it.

	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		// This method is called on end of playback instead of flush().
		// We need to do the same thing as flush(), so we just call it.
		if (p_soundtouch && st_enabled)
		{
			insert_chunks();
			if (buffered)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(),buffered);
				buffered = 0;
			}
			p_soundtouch->flush();
			insert_chunks();	
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			p_soundtouch = new SoundTouch;
			if (!p_soundtouch) return 0;
			p_soundtouch->setSampleRate(m_rate);
			p_soundtouch->setChannels(m_ch);
			p_soundtouch->setTempoChange(pitch_amount);
			st_enabled = true;
			if (pitch_amount== 0)st_enabled = false;
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
		
		}
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		if (!st_enabled) return true;
		while (sample_count)
		{    
			int todo = min(BUFFER_SIZE - buffered, sample_count);
			sample_buffer.write(src,todo*m_ch);
			src += todo * m_ch;
			buffered += todo;
			sample_count -= todo;
			if (buffered == BUFFER_SIZE)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
				buffered = 0;
				insert_chunks();
			}
		}
		return false;
	}

	virtual void flush() {
		if (p_soundtouch){
			p_soundtouch->clear();
		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		return (p_soundtouch && m_rate && st_enabled) ? ((double)(p_soundtouch->numSamples() + buffered) / (double)m_rate) : 0;
	}

	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopupTempo( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
		}
		catch(exception_io_data) {pitch = 0.0;}
	}
};

class dsp_rate : public dsp_impl_base
{
	SoundTouch * p_soundtouch;
	int m_rate, m_ch, m_ch_mask;
	float pitch_amount;
	circular_buffer<soundtouch::SAMPLETYPE>sample_buffer;
	pfc::array_t<soundtouch::SAMPLETYPE>samplebuf;
	unsigned buffered;
	bool st_enabled;
private:
	void insert_chunks()
	{
		uint samples = p_soundtouch->numSamples();
		if (!samples) return;
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		soundtouch::SAMPLETYPE * src = samplebuf.get_ptr();
		do
		{
			samples = p_soundtouch->receiveSamples(src, BUFFER_SIZE);
			if (samples > 0)
			{
				audio_chunk * chunk = insert_chunk(samples * m_ch);
				//	chunk->set_channels(m_ch,m_ch_mask);
				chunk->set_data_32(src, samples, m_ch, m_rate);
			}
		}
		while (samples != 0);
	}

public:
	dsp_rate( dsp_preset const & in ) : pitch_amount(0.00), m_rate( 0 ), m_ch( 0 ), m_ch_mask( 0 )
	{
		p_soundtouch=0;
		buffered=0;
		parse_preset( pitch_amount, in );
		st_enabled = true;
	}
	~dsp_rate(){
		if (p_soundtouch) delete p_soundtouch;
		p_soundtouch = 0;
	}

	// Every DSP type is identified by a GUID.
	static GUID g_get_guid() {
		// {8C12D81E-BB88-4056-B4C0-EAFA4E9F3B95}
		static const GUID guid = 
		{ 0x8c12d81e, 0xbb88, 0x4056, { 0xb4, 0xc0, 0xea, 0xfa, 0x4e, 0x9f, 0x3b, 0x95 } };
		return guid;
	}

	// We also need a name, so the user can identify the DSP.
	// The name we use here does not describe what the DSP does,
	// so it would be a bad name. We can excuse this, because it
	// doesn't do anything useful anyway.
	static void g_get_name(pfc::string_base & p_out) {
		p_out = "Playback Rate Shift";
	}

	virtual void on_endoftrack(abort_callback & p_abort) {
		// This method is called when a track ends.
		// We need to do the same thing as flush(), so we just call it.

	}

	virtual void on_endofplayback(abort_callback & p_abort) {
		// This method is called on end of playback instead of flush().
		// We need to do the same thing as flush(), so we just call it.
		if (p_soundtouch && st_enabled)
		{
			insert_chunks();
			if (buffered)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(),buffered);
				buffered = 0;
			}
			p_soundtouch->flush();
			insert_chunks();
		}
	}

	// The framework feeds input to our DSP using this method.
	// Each chunk contains a number of samples with the same
	// stream characteristics, i.e. same sample rate, channel count
	// and channel configuration.
	virtual bool on_chunk(audio_chunk * chunk, abort_callback & p_abort) {
		t_size sample_count = chunk->get_sample_count();
		audio_sample * src = chunk->get_data();

		if ( chunk->get_srate() != m_rate || chunk->get_channels() != m_ch || chunk->get_channel_config() != m_ch_mask )
		{
			m_rate = chunk->get_srate();
			m_ch = chunk->get_channels();
			m_ch_mask = chunk->get_channel_config();
			p_soundtouch = new SoundTouch;
			sample_buffer.set_size(BUFFER_SIZE*m_ch);
			if (!p_soundtouch) return 0;
			p_soundtouch->setSampleRate(m_rate);
			p_soundtouch->setChannels(m_ch);
			p_soundtouch->setRateChange(pitch_amount);
			st_enabled = true;
			if (pitch_amount== 0)st_enabled = false;
		}
		samplebuf.grow_size(BUFFER_SIZE * m_ch);
		if (!st_enabled) return true;
		while (sample_count)
		{    
			int todo = min(BUFFER_SIZE - buffered, sample_count);
			sample_buffer.write(src,todo*m_ch);
			src += todo * m_ch;
			buffered += todo;
			sample_count -= todo;
			if (buffered == BUFFER_SIZE)
			{
				sample_buffer.read(samplebuf.get_ptr(),buffered*m_ch);
				p_soundtouch->putSamples(samplebuf.get_ptr(), buffered);
				buffered = 0;
				insert_chunks();
			}
		}
		return false;
	}

	virtual void flush() {
		if (p_soundtouch){
			p_soundtouch->clear();
		}
		m_rate = 0;
		m_ch = 0;
		m_ch_mask = 0;
	}

	virtual double get_latency() {
		return (p_soundtouch && m_rate && st_enabled) ? ((double)(p_soundtouch->numSamples() + buffered) / (double)m_rate) : 0;
	}

	virtual bool need_track_change_mark() {
		return false;
	}

	static bool g_get_default_preset( dsp_preset & p_out )
	{
		make_preset( 0.0, p_out );
		return true;
	}
	static void g_show_config_popup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
	{
		::RunDSPConfigPopupRate( p_data, p_parent, p_callback );
	}
	static bool g_have_config_popup() { return true; }
	static void make_preset( float pitch, dsp_preset & out )
	{
		dsp_preset_builder builder; 
		builder << pitch; 
		builder.finish( g_get_guid(), out );
	}                        
	static void parse_preset(float & pitch, const dsp_preset & in)
	{
		try
		{
			dsp_preset_parser parser(in);
			parser >> pitch; 
		}
		catch(exception_io_data) {pitch = 0.0;}
	}
};

class CMyDSPPopupPitch : public CDialogImpl<CMyDSPPopupPitch>
{
public:
	CMyDSPPopupPitch( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_PITCH };
	enum
	{
		pitchmin = 0,
		pitchmax = 4800
		
	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_PITCH);
		slider_drytime.SetRange(0, pitchmax);

		{
			float  pitch;
			dsp_pitch::parse_preset(pitch, m_initData);
			pitch *= 100.00;
			slider_drytime.SetPos( (double)(pitch+2400));
			RefreshLabel( pitch/100.00);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnHScroll( UINT nSBCode, UINT nPos, CScrollBar pScrollBar )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-2400;
		pitch /= 100.00;
		{
			dsp_preset_impl preset;
			dsp_pitch::make_preset(pitch, preset );
			m_callback.on_preset_changed( preset );
		}
		RefreshLabel( pitch);
	}

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Pitch: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_float(pitch,0,2) << " semitones";
		::uSetDlgItemText( *this, IDC_PITCHINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopup( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupPitch popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}

class CMyDSPPopupRate : public CDialogImpl<CMyDSPPopupRate>
{
public:
	CMyDSPPopupRate( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_RATE };
	enum
	{
		pitchmin = 0,
		pitchmax = 150

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_RATE);
		slider_drytime.SetRange(0, pitchmax);

		{
			float  pitch;
			dsp_rate::parse_preset(pitch, m_initData);
			slider_drytime.SetPos( (double)(pitch+50));
			RefreshLabel( pitch);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnHScroll( UINT nSBCode, UINT nPos, CScrollBar pScrollBar )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-50;
		{
			dsp_preset_impl preset;
			dsp_rate::make_preset(pitch, preset );
			m_callback.on_preset_changed( preset );
		}
		RefreshLabel( pitch);
	}

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Playback Rate: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_int( pitch) << "%";
		::uSetDlgItemText( *this, IDC_RATEINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopupRate( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupRate popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}

class CMyDSPPopupTempo : public CDialogImpl<CMyDSPPopupTempo>
{
public:
	CMyDSPPopupTempo( const dsp_preset & initData, dsp_preset_edit_callback & callback ) : m_initData( initData ), m_callback( callback ) { }
	enum { IDD = IDD_TEMPO };
	enum
	{
		pitchmin = 0,
		pitchmax = 150

	};
	BEGIN_MSG_MAP( CMyDSPPopup )
		MSG_WM_INITDIALOG( OnInitDialog )
		COMMAND_HANDLER_EX( IDOK, BN_CLICKED, OnButton )
		COMMAND_HANDLER_EX( IDCANCEL, BN_CLICKED, OnButton )
		MSG_WM_HSCROLL( OnHScroll )
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM)
	{
		slider_drytime = GetDlgItem(IDC_TEMPO);
		slider_drytime.SetRange(0, pitchmax);

		{
			float  pitch;
			dsp_tempo::parse_preset(pitch, m_initData);
			slider_drytime.SetPos( (double)(pitch+50));
			RefreshLabel( pitch);
		}
		return TRUE;
	}

	void OnButton( UINT, int id, CWindow )
	{
		EndDialog( id );
	}

	void OnHScroll( UINT nSBCode, UINT nPos, CScrollBar pScrollBar )
	{
		float pitch;
		pitch = slider_drytime.GetPos()-50;
		{
			dsp_preset_impl preset;
			dsp_tempo::make_preset(pitch, preset );
			m_callback.on_preset_changed( preset );
		}
		RefreshLabel( pitch);
	}

	void RefreshLabel(float  pitch )
	{
		pfc::string_formatter msg; 
		msg << "Tempo: ";
		msg << (pitch < 0 ? "" : "+");
		msg << pfc::format_int( pitch) << "%";
		::uSetDlgItemText( *this, IDC_TEMPOINFO, msg );
		msg.reset();
	}
	const dsp_preset & m_initData; // modal dialog so we can reference this caller-owned object.
	dsp_preset_edit_callback & m_callback;
	CTrackBarCtrl slider_drytime,slider_wettime,slider_dampness,slider_roomwidth,slider_roomsize;
};
static void RunDSPConfigPopupTempo( const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback )
{
	CMyDSPPopupTempo popup( p_data, p_callback );
	if ( popup.DoModal(p_parent) != IDOK ) p_callback.on_preset_changed( p_data );
}



static dsp_factory_t<dsp_tempo> g_dsp_tempo_factory;
static dsp_factory_t<dsp_pitch> g_dsp_pitch_factory;
static dsp_factory_t<dsp_rate> g_dsp_rate_factory;