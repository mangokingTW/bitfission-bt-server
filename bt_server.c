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
	long int offset;
	char* source;
	char* target;
	FILE* lfile;
	char* torrent;
} cmd_arg;

unsigned long long rescue_write_size;
//unsigned long long image_head_size = 0;
std::map<std::string, long int> offset_map;
std::map<unsigned long long, unsigned long long> block_map;


void parse_arg(int argc, char **argv, cmd_arg *arg){
	static struct option long_options[] =
	{
		{"source", required_argument, 0, 's'},
		{"torrent", required_argument, 0, 't'},
		{"offset", required_argument, 0, 'o'},
		{"list", required_argument, 0, 'l'},
		{0,0,0,0}
	};
	int c;
	while((c = getopt_long(argc, argv, "l:o:t:s:", long_options, NULL)) != -1 ){
		switch(c){
			case 'l':
				arg->lfile = fopen(optarg, "r");
				break;
			case 'o':
				sscanf(optarg, "%ld", &(arg->offset));
				break;
			case 's':
				arg->source = optarg;
				break;
			case 't':
				arg->torrent = optarg;
				break;
			default:
				exit(-1);
		}
	}
}


namespace lt = libtorrent;

struct raw_storage : lt::storage_interface {
	raw_storage(lt::file_storage const& fs, const std::string tp) : m_files(fs), target_image(tp) {
	}
	// Open disk fd
	void initialize(lt::storage_error& se)
	{
		this->fd = open(target_image.c_str(), O_RDONLY | O_LARGEFILE | O_NONBLOCK);
		if(this->fd <= 0){
			// Failed handle
			std::cerr << "Failed to open " << target_image << std::endl;

			// TODO exit
		}
		image_head_size = offset_map[target_image];
		return;
	}

	bool has_any_file(lt::storage_error& ec) 
	{
		std::cerr << "fd: " << this->fd << " " << image_head_size << std::endl;
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
	long int image_head_size;
	const std::string target_image;
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


	lt::add_torrent_params atp;
	lt::session ses;
	lt::error_code ec;
	lt::settings_pack set;
	int timeout_ezio = 15; // Default timeout (min)
	int seed_limit_ezio = 3; // Default seeding ratio limit
	int max_upload_ezio = 4;
	int max_connection_ezio = max_upload_ezio + 2;
	int max_contact_tracker_times = 30; // Max error times for scrape tracker


	set.set_bool(lt::settings_pack::enable_dht, false);
	set.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6666");
	ses.apply_settings(set);
	std::list<lt::torrent_handle> handle_list;

	arg.lfile = NULL;
	parse_arg(argc, argv, &arg);
	if( arg.lfile == NULL ) {
		std::string torrent = arg.torrent;
		offset_map[torrent] = arg.offset;
		atp.ti = boost::make_shared<lt::torrent_info>(torrent, boost::ref(ec), 0);
		atp.storage = raw_storage_constructor;
		atp.save_path = arg.source;
		atp.flags |= atp.flag_seed_mode;
		lt::torrent_handle ahandle = ses.add_torrent(atp);
		handle_list.push_back(ahandle);
	}
	else {
		char atorrent[128] = {'\0'};
		char asource[128] = {'\0'};
		long int offset = 0;
		while( fscanf(arg.lfile, "%ld %128s %128s", &offset, &asource, &atorrent) != EOF ){
			std::string torrent = atorrent;
			offset_map[std::string(asource)] = offset;
			atp.ti = boost::make_shared<lt::torrent_info>(torrent, boost::ref(ec), 0);
			atp.storage = raw_storage_constructor;
			atp.save_path = asource;
			atp.flags |= atp.flag_seed_mode;
			lt::torrent_handle ahandle = ses.add_torrent(atp);
			ahandle.set_max_uploads(max_upload_ezio);
			ahandle.set_max_connections(max_connection_ezio);
			handle_list.push_back(ahandle);
		}
	}
	
	// Start high performance seed
	lt::high_performance_seed(set);
	ses.apply_settings(set);
	std::cout << "Start high-performance seeding" << std::endl;

	for (;;) {
		for( lt::torrent_handle handle : handle_list ){
			lt::torrent_status status = handle.status();
			unsigned long progress = 0;
			int utime = status.time_since_upload;
			int dtime = status.time_since_download;
			// ses.set_alert_mask(lt::alert::tracker_notification | lt::alert::error_notification);
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);
			progress = status.progress * 100;
			std::cout << std::fixed
				<< status.save_path << " "
				<< "[P: " << progress << "%] "
				//<< "[D: " << std::setprecision(2) << (float)status.download_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
				//<< "[T: " << (int)status.active_time  << " secs] "
				<< "[U: " << std::setprecision(2) << (float)status.upload_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
				<< "[T: " << (int)status.seeding_time  << " secs] "
				<< status.state
				<< std::endl
				<< std::flush;
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	std::cout << "\nDone, shutting down" << std::endl;

	
	return 0;
}
