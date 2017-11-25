#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <thread>
#include <chrono>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/storage.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/peer_info.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

typedef struct{
	long int header;
	char* source;
	char* target;
	bool raw;
	char* torrent;
} cmd_arg;

unsigned long long rescue_write_size;
unsigned long long image_head_size = 0;
std::map<unsigned long long, unsigned long long> block_map;


void parse_arg(int argc, char **argv, cmd_arg *arg){
	static struct option long_options[] =
	{
		{"source", required_argument, 0, 's'},
		{"torrent", required_argument, 0, 't'},
		{"header", required_argument, 0, 'h'},
		{"raw", no_argument, 0, 'r'},
		{0,0,0,0}
	};
	int c;
	while((c = getopt_long(argc, argv, "h:t:s:r", long_options, NULL)) != -1 ){
		switch(c){
			case 'h':
				sscanf(optarg, "%ld", &(arg->header));
				break;
			case 's':
				arg->source = optarg;
				break;
			case 't':
				arg->torrent = optarg;
				break;
			case 'r':
				arg->raw = true;
				break;
			default:
				exit(-1);
		}
	}
}


namespace lt = libtorrent;

struct raw_storage : lt::storage_interface {
	raw_storage(lt::file_storage const& fs, const std::string tp) : m_files(fs), target_partition(tp) {
	}
	// Open disk fd
	void initialize(lt::storage_error& se)
	{
		this->fd = open(target_partition.c_str(), O_RDONLY | O_LARGEFILE | O_NONBLOCK);
		if(this->fd <= 0){
			// Failed handle
			std::cerr << "Failed to open " << target_partition << std::endl;

			// TODO exit
		}
		return;
	}

	bool has_any_file(lt::storage_error& ec) 
	{
		std::cerr << "fd: " << this->fd << std::endl;
		return true;
	}

	int readv(lt::file::iovec_t const* bufs, int num_bufs, int piece, int offset, int flags, lt::storage_error& ec)
	{
		int index = 0;
		int i = 0;
		int ret = 0;
		unsigned long long device_offset = 0;
		unsigned long long fd_offset = 0; // A fd' point we read data from fd from 
		unsigned long long cur_offset = 0; // A pieces' point we have to write data until
		unsigned long long remain_len = 0;
		unsigned long long piece_sum = 0;
		unsigned long long data_len = 0;
		char *data_buf, *data_ptr = NULL;
		char filename[33]; // Should be the max length of file name
		
		// Caculate the length of all bufs
		for( i = 0 ; i < num_bufs ; i ++){
			data_len += bufs[i].iov_len;
		}
		data_buf = (char *)malloc(data_len);
		
		fd_offset = image_head_size + offset + piece * std::uint64_t(m_files.piece_length());
		ret = pread(this->fd, data_buf, data_len, fd_offset);
		// Copy data_buf to bufs
		data_ptr = data_buf;
		for( i = 0 ; i < num_bufs ; i ++){
			memcpy(bufs[i].iov_base, data_ptr, bufs[i].iov_len);
			data_ptr += bufs[i].iov_len;
		}
		free(data_buf);
		return ret;
	}

	// Not need
	int writev(lt::file::iovec_t const* bufs, int num_bufs, int piece, int offset, int flags, lt::storage_error& ec)
	{
		return 0;
	}

	void rename_file(int index, std::string const& new_filename, lt::storage_error& ec)
	{ assert(false); return ; }

	int move_storage(std::string const& save_path, int flags, lt::storage_error& ec) { return 0; }
	bool verify_resume_data(lt::bdecode_node const& rd
					, std::vector<std::string> const* links
					, lt::storage_error& error) 
	{
		return false;
	}
	void write_resume_data(lt::entry& rd, lt::storage_error& ec) const { return ; }
	void set_file_priority(std::vector<boost::uint8_t> const& prio, lt::storage_error& ec) {return ;}
	void release_files(lt::storage_error& ec) { return ; }
	void delete_files(int i, lt::storage_error& ec) { return ; }

	bool tick () { return false; };


	lt::file_storage m_files;
	int fd = 0;
	const std::string target_partition;
};

lt::storage_interface* raw_storage_constructor(lt::storage_params const& params)
{
		lt::storage_interface* tmp =  new raw_storage(*params.files, params.path);
		lt::storage_error se;
		tmp->initialize(se);
		return tmp;
}

int main(int argc, char **argv){
	unsigned long *bitmap = NULL;
	cmd_arg arg;
	int dfr;

	parse_arg(argc, argv, &arg);

	if(!arg.raw){
		image_head_size = arg.header;
	}

	lt::add_torrent_params atp;
	lt::session ses;
	lt::error_code ec;
	lt::settings_pack set;
	std::string torrent = arg.torrent;
	int timeout_ezio = 15; // Default timeout (min)
	int seed_limit_ezio = 3; // Default seeding ratio limit
	int max_upload_ezio = 4;
	int max_connection_ezio = max_upload_ezio + 2;
	int max_contact_tracker_times = 30; // Max error times for scrape tracker


	set.set_bool(lt::settings_pack::enable_dht, false);
	set.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6666");
	ses.apply_settings(set);
	atp.ti = boost::make_shared<lt::torrent_info>(torrent, boost::ref(ec), 0);
	atp.storage = raw_storage_constructor;
	atp.save_path = arg.source;
	atp.flags |= atp.flag_seed_mode;
	
	
	unsigned long last_progess = 0, progress = 0;
	lt::torrent_status status;

	lt::torrent_handle handle = ses.add_torrent(atp);
	handle.set_max_uploads(max_upload_ezio);
	handle.set_max_connections(max_connection_ezio);

	for(;;){
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);

		status = handle.status();
		// progress
		last_progess = progress;
		progress = status.progress * 100;
		//show_progress += progress - last_progess;
		std::cout << std::fixed << "\r"
			<< "[P: " << progress << "%] "
			<< "[D: " << std::setprecision(2) << (float)status.download_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
			<< "[DT: " << (int)status.active_time  << " secs] "
			<< "[U: " << std::setprecision(2) << (float)status.upload_payload_rate / 1024 / 1024 /1024 *60 << " GB/min] "
			<< "[UT: " << (int)status.seeding_time  << " secs] "
			<< std::flush;

		for (lt::alert const* a : alerts) {
			// std::cout << a->message() << std::endl;
			// if we receive the finished alert or an error, we're done
			if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
				goto done;
			}
			if (status.is_finished) {
				goto done;
			}
			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
				std::cerr << "Error" << std::endl;
				return 1;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}	

	done:
	std::cout << std::endl;


	// Start high performance seed
	lt::high_performance_seed(set);
	ses.apply_settings(set);
	std::cout << "Start high-performance seeding" << std::endl;

	// seed until idle (secs)
	int timeout = timeout_ezio * 60;

	// seed until seed rate
	boost::int64_t seeding_rate_limit = seed_limit_ezio;
	boost::int64_t total_size = handle.torrent_file()->total_size();

	int fail_contact_tracker = 0;
	for (;;) {
		status = handle.status();
		int utime = status.time_since_upload;
		int dtime = status.time_since_download;
		boost::int64_t total_payload_upload = status.total_payload_upload;
		// ses.set_alert_mask(lt::alert::tracker_notification | lt::alert::error_notification);
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);
		progress = status.progress * 100;
		std::cout << std::fixed << "\n\r"
			<< "[P: " << progress << "%] "
			/*
			<< "[D: " << std::setprecision(2) << (float)status.download_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
			<< "[T: " << (int)status.active_time  << " secs] "
			*/
			<< "[U: " << std::setprecision(2) << (float)status.upload_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
			<< "[T: " << (int)status.seeding_time  << " secs] "
			<< status.state
			<< std::flush;

		if(utime == -1 && timeout < dtime){
			break;
		}
		else if(timeout < utime){
			break;
		}
		else if(seeding_rate_limit < (total_payload_upload / total_size)){
			break;
		}

		handle.scrape_tracker();
		for (lt::alert const* a : alerts) {
			if (lt::alert_cast<lt::scrape_failed_alert>(a)) {
				++fail_contact_tracker;;
			}
		}

		if(fail_contact_tracker > max_contact_tracker_times){
	                std::cout << "\nTracker is gone! Finish seeding!" << std::endl;
			break;
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	std::cout << "\nDone, shutting down" << std::endl;

	
	return 0;
}
