#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <thread>
#include <chrono>
#include <openssl/sha.h>

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

#include "partclone.h"

typedef struct{
	char* source;
	char* target;
	char* torrent;
} cmd_arg;

cmd_opt opt;
unsigned long long rescue_write_size;
unsigned long long image_head_size;
file_system_info fs_info;
std::map<unsigned long long, unsigned long long> block_map;


void parse_arg(int argc, char **argv, cmd_arg *arg){
	static struct option long_options[] =
	{
		{"source", required_argument, 0, 's'},
		{"torrent", required_argument, 0, 't'}
	};
	int c;
	while((c = getopt_long(argc, argv, "t:s:", long_options, NULL)) != -1 ){
		switch(c){
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
	raw_storage(lt::file_storage const& fs, const std::string tp) : m_files(fs), target_partition(tp) {
	}
	// Open disk fd
	void initialize(lt::storage_error& se)
	{
		/*this->fd = open(target_partition.c_str(), O_RDONLY | O_LARGEFILE );
		if(this->fd <= 0){
			// Failed handle
			std::cerr << "Failed to open " << target_partition << std::endl;

			// TODO exit
		}
		*/
		return;
	}

	bool has_any_file(lt::storage_error& ec) 
	{
		printf("fucckkkkkkkkk any file\n");
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
		
		this->fd = open(target_partition.c_str(), O_RDONLY | O_NONBLOCK | O_LARGEFILE);
		if( this->fd < 0 ) abort();
		
		// Get file name from offset
		index = m_files.file_index_at_offset( piece * std::uint64_t(m_files.piece_length()) + offset);
		memcpy( filename, m_files.file_name_ptr(index), m_files.file_name_len(index));
		filename[m_files.file_name_len(index)] = 0;
		sscanf(filename,"%llx", &device_offset);
		// Caculate total piece size of previous files
		for( i = 0 ; i < index; i++ )
			piece_sum += m_files.file_size(i);
		
		// Caculate the length of all bufs
		for( i = 0 ; i < num_bufs ; i ++){
			data_len += bufs[i].iov_len;
		}
		data_buf = (char *)malloc(data_len);
		
		fd_offset = image_head_size + offset + piece * std::uint64_t(m_files.piece_length());
		ret = pread(this->fd, data_buf, data_len, fd_offset);
		/*
		printf("fileoffset: %llx\n",fd_offset);
		printf("deviceoffset: %llx\n",device_offset);
		printf("pieceoffset: %llx\n",fd_offset - image_head_size);
		printf("piece: %llx\n",piece);
		printf("ret: %llx\n",ret);
		printf("data: ");
		for( int i = 0 ; i < SHA_DIGEST_LENGTH ; i++){
			printf("%02x",data_buf[i]);
		}
		printf("\n======================\n\n");
		*/
		// Copy data_buf to bufs
		data_ptr = data_buf;
		for( i = 0 ; i < num_bufs ; i ++){
			memcpy(bufs[i].iov_base, data_ptr, bufs[i].iov_len);
			data_ptr += bufs[i].iov_len;
		}

		free(data_buf);
		close(this->fd);
		return ret;
	}

	// Not need
	int writev(lt::file::iovec_t const* bufs, int num_bufs, int piece, int offset, int flags, lt::storage_error& ec)
	{
		printf("fucckkkkkkkkk\n");
		return 0;
	}

	void rename_file(int index, std::string const& new_filename, lt::storage_error& ec)
	{ assert(false); return ; }

	int move_storage(std::string const& save_path, int flags, lt::storage_error& ec) { return 0; }
	bool verify_resume_data(lt::bdecode_node const& rd
					, std::vector<std::string> const* links
					, lt::storage_error& error) 
	{
		printf("fucckkkkkkkkk\n");
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
	return new raw_storage(*params.files, params.path);
}

int main(int argc, char **argv){
	//file_system_info fs_info;
	image_options img_opt;
	image_head_v2 img_head;
	unsigned long *bitmap = NULL;
	cmd_arg arg;
	int dfr;
	//long long int image_head_size = 0;
	//std::map<unsigned long long, unsigned long long> block_map;

	parse_arg(argc, argv, &arg);

	init_fs_info(&fs_info);
	if((dfr = open(arg.source, O_RDONLY | O_LARGEFILE)) == -1){
		perror("unable to open source");
		exit(-1);
	}
	load_image_desc(&dfr, &opt, &img_head, &fs_info, &img_opt);
	printf("Total Blocks: %lld\n",fs_info.totalblock);
	bitmap = pc_alloc_bitmap(fs_info.totalblock);
	load_image_bitmap(&dfr, opt, fs_info, img_opt, bitmap);

	unsigned long long counter = 0;
	unsigned long long cur_block = 0;
	unsigned long long save_block = 0;
	unsigned long long cur_bitmap = 0;
	unsigned long long save_bitmap = 0;
	do {
		for( ; cur_block < fs_info.totalblock &&
			!pc_test_bit(cur_block, bitmap, fs_info.totalblock);
			cur_block++);
		if( cur_block == fs_info.totalblock ) break;
		save_block = cur_block;
		save_bitmap = cur_bitmap;
		for( ; cur_block < fs_info.totalblock &&
			pc_test_bit(cur_block, bitmap, fs_info.totalblock);
			cur_block++, cur_bitmap++);
		block_map[save_block*fs_info.block_size] = save_bitmap*fs_info.block_size;
		printf("disk block(%llx) is on bitmap(%llx) and len(%llx)\n", save_block*fs_info.block_size, save_bitmap,(cur_bitmap-save_bitmap)*fs_info.block_size);
	} while ( cur_block < fs_info.totalblock );
	printf("Used Blocks: %lld\n", cur_bitmap);
	printf("Size of image header: %lld\n", sizeof(image_head_v2));
	image_head_size = lseek(dfr, 0, SEEK_CUR);
	printf("ftell: %lld\n", image_head_size);
	printf("Block size: %lld\n", fs_info.block_size);
	printf("Total size: %lld\n",image_head_size+cur_bitmap*fs_info.block_size);
	printf("File size: %lld\n",lseek(dfr, 0, SEEK_END));
	close(dfr);
	

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


	atp.save_path = arg.source;
	set.set_bool(lt::settings_pack::enable_dht, false);
	set.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6666");
	atp.ti = boost::make_shared<lt::torrent_info>(torrent, boost::ref(ec), 0);
	atp.storage = raw_storage_constructor;
	//atp.flags |= atp.flag_seed_mode;
	
	
	unsigned long last_progess = 0, progress = 0;
	lt::torrent_status status;

	//lt::high_performance_seed(set);
	ses.apply_settings(set);
	std::cout << "Start high-performance seeding" << std::endl;
	lt::torrent_handle handle = ses.add_torrent(atp);

	// seed until idle (secs)
	int timeout = timeout_ezio * 60;

	// seed until seed rate

	int fail_contact_tracker = 0;
	for (;;) {
		status = handle.status();
		int utime = status.time_since_upload;
		int dtime = status.time_since_download;
		boost::int64_t total_payload_upload = status.total_payload_upload;
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);

		std::cout << std::fixed << "\n\r"
			<< "[U: " << std::setprecision(2) << (float)status.upload_payload_rate / 1024 / 1024 /1024 * 60 << " GB/min] "
			<< "[T: " << (int)status.seeding_time  << " secs] "
			<< status.state
			<< status.error
			<< std::flush;

		if(utime == -1 && timeout < dtime){
			break;
		}
		else if(timeout < utime){
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

	return 0;
}
