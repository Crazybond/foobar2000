#include "targetver.h"
#include <Windows.h>
#include "../ATLHelpers/ATLHelpers.h"
#include "../shared/shared.h"
#include "resource.h"
#include "hash/sha1.h"

using namespace pfc;
// {3F15B59B-6729-454B-8537-6A8257D0FCF5}
static const GUID guid = 
{ 0x3f15b59b, 0x6729, 0x454b, { 0x85, 0x37, 0x6a, 0x82, 0x57, 0xd0, 0xfc, 0xf5 } };
// {5342A231-84B9-4ACC-8C4B-610E7BCA1331}
static const GUID guid_mygroup = 
{ 0x5342a231, 0x84b9, 0x4acc, { 0x8c, 0x4b, 0x61, 0xe, 0x7b, 0xca, 0x13, 0x31 } };

static contextmenu_group_popup_factory g_mygroup(guid_mygroup, contextmenu_groups::root, "Audio data hasher", 0);


VALIDATE_COMPONENT_FILENAME("foo_audiohasher.dll");
DECLARE_COMPONENT_VERSION(
	"Audio Hasher",
	"0.1",
	"A audio hashing/checksum component for foobar2000 1.1 ->\n"
	"Copyright (C) 2012 Brad Miller\n"
	"http://mudlord.info\n"
);

bool decode_file(char const * p_path,audio_chunk& chunk, abort_callback & p_abort)
{
    try
    {
        input_helper helper;
        file_info_impl info;
        // open input
        helper.open(service_ptr_t<file>(), make_playable_location(p_path, 0), input_flag_simpledecode, p_abort);
        helper.get_info(0, info, p_abort);
        if (info.get_length() <= 0)
            throw pfc::exception("Track length invalid");
        audio_chunk_impl chunk;
        if (!helper.run(chunk, p_abort)) return false;
        t_uint64 length_samples = audio_math::time_to_samples(info.get_length(), chunk.get_sample_rate());
        while (true)
        {
            bool decode_done = !helper.run(chunk, p_abort);
            if (decode_done) break;
        }
        return true;
    }
    catch (const exception_aborted &) {throw;}
    catch (const std::exception & exc)
    {
        console::formatter() << exc << ": " << p_path;
        return false;
    }
};

class calculate_hash : public threaded_process_callback {
public:
	calculate_hash(metadb_handle_list_cref items) : m_items(items), m_peak() {
	}

	void on_init(HWND p_wnd) {}
	void run(threaded_process_status & p_status,abort_callback & p_abort) {
		try {
			const t_uint32 decode_flags = input_flag_no_seeking | input_flag_no_looping; // tell the decoders that we won't seek and that we don't want looping on formats that support looping.
			input_helper input;
			audio_hash.set_count(m_items.get_size());
			for(t_size walk = 0; walk < m_items.get_size(); ++walk) {

				p_abort.check(); // in case the input we're working with fails at doing this
				p_status.set_progress(walk, m_items.get_size());
				p_status.set_progress_secondary(0);
				p_status.set_item_path( m_items[walk]->get_path() );
				input.open(NULL, m_items[walk], decode_flags, p_abort);
				
				double length;
				{ // fetch the track length for proper dual progress display;
					file_info_impl info;
					// input.open should have preloaded relevant info, no need to query the input itself again.
					// Regular get_info() may not retrieve freshly loaded info yet at this point (it will start giving the new info when relevant info change callbacks are dispatched); we need to use get_info_async.
					if (m_items[walk]->get_info_async(info)) length = info.get_length();
					else length = 0;
				}

				memset( &ctx, 0, sizeof( sha1_context ) );
				sha1_starts( &ctx );
				audio_chunk_impl_temporary l_chunk;
				
				double decoded = 0;
				while(input.run(l_chunk, p_abort)) { // main decode loop
				    sha1_update(&ctx,(unsigned char*)l_chunk.get_data(),l_chunk.get_data_length());
					//m_peak = l_chunk.get_peak(m_peak);
					if (length > 0) { // don't bother for unknown length tracks
						decoded += l_chunk.get_duration();
						if (decoded > length) decoded = length;
						p_status.set_progress_secondary_float(decoded / length);
					}
					p_abort.check(); // in case the input we're working with fails at doing this
				}
				unsigned char sha1sum[20] ={0};
				sha1_finish(&ctx,sha1sum);
				pfc::string_formatter msg;
			    for(int i = 0; i < 20; i++ )
				{
				 
				  msg <<pfc::format_hex(sha1sum[i]);
				  audio_hash[walk] =msg;
				}
				



			}
		} catch(std::exception const & e) {
			m_failMsg = e.what();
		}
	}
	void on_done(HWND p_wnd,bool p_was_aborted) {
		if (!p_was_aborted) {
			if (!m_failMsg.is_empty()) {
				popup_message::g_complain("Peak scan failure", m_failMsg);
			} else {
				pfc::string_formatter result;

				
				for(t_size walk = 0; walk < m_items.get_size(); ++walk) {
				    result << "Filename:\n";
					result << m_items[walk] << "\n";
					result << "Hash: " << audio_hash[walk] << "\n";
				}
				popup_message::g_show(result,"Hashing results");
			}
		}
	}
private:
	audio_sample m_peak;
	pfc::string8 m_failMsg;
	pfc::array_t<string8> audio_hash;
	sha1_context ctx;
	const metadb_handle_list m_items;
};

void RunCalculatePeak(metadb_handle_list_cref data) {
	try {
		if (data.get_count() == 0) throw pfc::exception_invalid_params();
		service_ptr_t<threaded_process_callback> cb = new service_impl_t<calculate_hash>(data);
		static_api_ptr_t<threaded_process>()->run_modeless(
			cb,
			threaded_process::flag_show_progress_dual | threaded_process::flag_show_item | threaded_process::flag_show_abort,
			core_api::get_main_window(),
			"Calculating audio data hashes...");
	} catch(std::exception const & e) {
		popup_message::g_complain("Could not start audio hashing process", e);
	}
}

class audiohasher : public contextmenu_item_simple {
public:
    GUID get_parent() {return guid_mygroup;}
	unsigned get_num_items() {return 1;}

	void get_item_name(unsigned p_index,pfc::string_base & p_out) {
		switch(p_index) {
			case 0: p_out = "Calculate audio data hash"; break;
			default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	void context_command(unsigned p_index,metadb_handle_list_cref p_data,const GUID& p_caller) {
		switch(p_index) {
			case 0:
				RunCalculatePeak(p_data);
				break;
			default:
				uBugCheck();
		}
	}

	GUID get_item_guid(unsigned p_index) {
		// These GUIDs identify our context menu items. Substitute with your own GUIDs when reusing code.
		static const GUID guid_test1 ={ 0xe87641bb, 0xbc5f, 0x468c, { 0x95, 0x1c, 0x8c, 0xa3, 0xf6, 0x8a, 0x36, 0xf1 } };
		switch(p_index) {
			case 0: return guid_test1;
			default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}

	}
	bool get_item_description(unsigned p_index,pfc::string_base & p_out) {
		switch(p_index) {
			case 0:
				p_out = "Calculates audio data hashes on selected files.";
				return true;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

};

static contextmenu_item_factory_t<audiohasher> g_myitem_factory;
