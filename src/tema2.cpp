#include <mpi.h>
#include <pthread.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define FILE_REQ_TAG 	1 // tag for file request
#define D_O_DONE_TAG	2 // tag for one file downloaded
#define D_A_DONE_TAG	3 // tag for all files downloaded
#define A_C_DONE_TAG	4 // tag for all clients done downloading

#define HASH_REQ_TAG	11 // tag for hash request
#define HASH_RSP_TAG	12 // tag for hash response

#define DOWNLOAD_TAG	21 // tag for download thread
#define UPDATE_TAG		23 // tag for update request from downloader

using namespace std;

struct args_struct {
	void* point1;
	void* point2;
	int rank;
	pthread_mutex_t *mutex;
};

void *download_thread_func(void *arg)
{
	int rank = ((args_struct*)arg)->rank;
	vector<string> leech_files = *(vector<string>*)((args_struct*)arg)->point1;
	map<string, vector<string>>* seed_files = (map<string, vector<string>>*)((args_struct*)arg)->point2;
	pthread_mutex_t *mutex = ((args_struct*)arg)->mutex;

	int leeched_segments = 0;

	for (auto file : leech_files) {
		// send to tracker the file that is going to be downloaded
		MPI_Send(file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, FILE_REQ_TAG, MPI_COMM_WORLD);

		// receive number of seeders
		int num_seeders;
		MPI_Recv(&num_seeders, 1, MPI_INT, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		// receive list of seeders
		vector<int> seeders = vector<int>(num_seeders);
		MPI_Recv(seeders.data(), num_seeders, MPI_INT, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		// receive number of peers
		int num_peers;
		MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		vector<int> peers;
		// receive list of peers
		if (num_peers > 0) {
			peers = vector<int>(num_peers);
			MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}

		// receive number of segments
		int num_segments;
		MPI_Recv(&num_segments, 1, MPI_INT, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		// receive hashes
		vector<string> hashes = vector<string>(num_segments);
		for (int i = 0; i < num_segments; i++) {
			char hash[HASH_SIZE + 1];
			memset(hash, 0, HASH_SIZE + 1);
			MPI_Recv(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, DOWNLOAD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			hashes[i] = string(hash);
		}

		ofstream fout("client" + to_string(rank) + "_" + file);

		for (auto hash : hashes) {
			bool found = false;

			while (!found) {
				// send to peers the request for the hash
				for (auto peer : peers) {
					if (peer == rank) {
						continue;
					}

					MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, peer, HASH_REQ_TAG, MPI_COMM_WORLD);

					// receive from client the availability of the hash
					MPI_Status status;
					char r;
					MPI_Recv(&r, 1, MPI_CHAR, peer, HASH_RSP_TAG, MPI_COMM_WORLD, &status);

					if (r == 1) {
						found = true;
						leeched_segments++;

						// add segment hash to seed list
						pthread_mutex_lock(mutex);
						(*seed_files)[file].push_back(hash);
						pthread_mutex_unlock(mutex);

						// print hash
						fout << hash << endl;

						if (leeched_segments % 10 == 0) {
							// send to tracker the request for updated seeder list
							MPI_Send(file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);

							// receive number of seeders
							MPI_Recv(&num_seeders, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

							seeders.resize(num_seeders);

							// receive updated list of seeders
							MPI_Recv(seeders.data(), num_seeders, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						
							// receive number of peers
							MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

							if (num_peers > 0) {
								peers.resize(num_peers);

								// receive updated list of peers
								MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
							}
						}
						break;
					}
				}

				if (found) {
					break;
				}

				// if no peer has the hash, ask seeders
				for (auto seeder : seeders) {
					MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, seeder, HASH_REQ_TAG, MPI_COMM_WORLD);

					// receive from client the availability of the hash
					MPI_Status status;
					char r;
					MPI_Recv(&r, 1, MPI_CHAR, seeder, HASH_RSP_TAG, MPI_COMM_WORLD, &status);

					if (r == 1) {
						found = true;
						leeched_segments++;

						// add segment hash to seed list
						pthread_mutex_lock(mutex);
						(*seed_files)[file].push_back(hash);
						pthread_mutex_unlock(mutex);

						// print hash
						fout << hash << endl;

						if (leeched_segments % 10 == 0) {
							// send to tracker the request for updated seeder list
							MPI_Send(file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);

							// receive number of seeders
							MPI_Recv(&num_seeders, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

							seeders.resize(num_seeders);

							// receive updated list of seeders
							MPI_Recv(seeders.data(), num_seeders, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						
							// receive number of peers
							MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

							if (num_peers > 0) {
								peers.resize(num_peers);

								// receive updated list of peers
								MPI_Recv(peers.data(), num_peers, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
							}
						}
						break;
					}
				}
			}
		}

		fout.close();

		// send to tracker signal that file "file" is downloaded
		MPI_Send(file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, D_O_DONE_TAG, MPI_COMM_WORLD);
	}

	char dummy[MAX_FILENAME];
	// send to tracker signal that all files are downloaded
	MPI_Send(&dummy, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, D_A_DONE_TAG, MPI_COMM_WORLD);

	return NULL;
}

void *upload_thread_func(void *arg)
{
	map<string, vector<string>>* seed_files = (map<string, vector<string>>*)((args_struct*)arg)->point2;
	pthread_mutex_t *mutex = ((args_struct*)arg)->mutex;

	bool done = false;

	while (!done) {
		char hash[HASH_SIZE + 1];
		MPI_Status status;

		MPI_Recv(&hash, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, HASH_REQ_TAG, MPI_COMM_WORLD, &status);
		bool found = false;
		char c = 0;

		switch(status.MPI_SOURCE) {
			case TRACKER_RANK:
				done = true;
				break;

			default:
				// check if the hash is in the seed list
				pthread_mutex_lock(mutex);
				for (auto entry : *seed_files) {
					if (find(entry.second.begin(), entry.second.end(), string(hash)) != entry.second.end()) {
						found = true;
						break;
					}
				}
				pthread_mutex_unlock(mutex);

				found ? c = 1 : c = 0;

				MPI_Send(&c, 1, MPI_CHAR, status.MPI_SOURCE, HASH_RSP_TAG, MPI_COMM_WORLD);
				break;
		}
	}

	return NULL;
}

void tracker(int numtasks, int rank) {
	// files with their seeders and peers
	map<string, vector<int>> seeders;
	map<string, vector<int>> peers;

	// count the number of clients that finished downloading
	int counter = 0;

	// files with their hashes for eatch segment
	map<string, vector<string>> files;

	for (int i = 0; i < numtasks; i++) {
		if (i == TRACKER_RANK) {
			continue;
		}

		// receive number of seed files
		int num_files;
		MPI_Recv(&num_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		for (int j = 0; j < num_files; j++) {
			// receive filename
			char filename[MAX_FILENAME];
			memset(filename, 0, MAX_FILENAME);
			MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			// add seeder to file
			if (seeders.find(filename) == seeders.end()) {
				seeders[filename] = vector<int>();
			}

			seeders[filename].push_back(i);

			// receive number of segments
			int num_segments;
			MPI_Recv(&num_segments, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			if (files.find(filename) == files.end()) {
				files[filename] = vector<string>();
			}

			for (int k = 0; k < num_segments; k++) {
				char hash[HASH_SIZE + 1];
				memset(hash, 0, HASH_SIZE + 1);

				MPI_Recv(hash, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				// add hash to file if it isn t already added
				if (find(files[filename].begin(), files[filename].end(), string(hash)) == files[filename].end()) {
					files[filename].push_back(string(hash));
				}
			}
		}
	}

	int signal = 1;
	MPI_Bcast(&signal, 1, MPI_INT, TRACKER_RANK, MPI_COMM_WORLD);

	bool done = false;

	while (!done) {
		char filename[MAX_FILENAME + 1];
		memset(filename, 0, MAX_FILENAME + 1);
		MPI_Status status;

		MPI_Recv(&filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

		int num_seeders, num_peers, num_segments;

		switch(status.MPI_TAG) {
			case FILE_REQ_TAG:
				// send number of seeders
				num_seeders = seeders[filename].size();
				MPI_Send(&num_seeders, 1, MPI_INT, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);

				// send list of seeders
				MPI_Send(seeders[filename].data(), num_seeders, MPI_INT, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);

				// send number of peers
				num_peers = peers[filename].size();
				MPI_Send(&num_peers, 1, MPI_INT, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);

				if (num_peers > 0) {
					// send list of peers
					MPI_Send(peers[filename].data(), num_peers, MPI_INT, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);
				}

				// mark the client as peer
				peers[filename].push_back(status.MPI_SOURCE);

				// send number of segments
				num_segments = files[filename].size();
				MPI_Send(&num_segments, 1, MPI_INT, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);

				// send hashes
				for (auto hash : files[filename]) {
					MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, DOWNLOAD_TAG, MPI_COMM_WORLD);
				}
				break;

			case UPDATE_TAG:
				// send number of seeders
				num_seeders = seeders[filename].size();
				MPI_Send(&num_seeders, 1, MPI_INT, status.MPI_SOURCE, UPDATE_TAG, MPI_COMM_WORLD);

				// send list of seeders
				MPI_Send(seeders[filename].data(), num_seeders, MPI_INT, status.MPI_SOURCE, UPDATE_TAG, MPI_COMM_WORLD);

				// send number of peers
				num_peers = peers[filename].size();
				MPI_Send(&num_peers, 1, MPI_INT, status.MPI_SOURCE, UPDATE_TAG, MPI_COMM_WORLD);

				if (num_peers > 0) {
					// send list of peers
					MPI_Send(peers[filename].data(), num_peers, MPI_INT, status.MPI_SOURCE, UPDATE_TAG, MPI_COMM_WORLD);
				}

				break;

			case D_O_DONE_TAG:
				// remove peer from file list
				if (find(peers[filename].begin(), peers[filename].end(), status.MPI_SOURCE) != peers[filename].end()) {
					peers[filename].erase(find(peers[filename].begin(), peers[filename].end(), status.MPI_SOURCE));
				}

				// mark the client as seed
				seeders[filename].push_back(status.MPI_SOURCE);
				break;

			case D_A_DONE_TAG:
				counter++;

				if (counter == numtasks - 1) {
					done = true;
				}
				break;

			default:
				break;
		}
	}

	char dummy[HASH_SIZE];
	// send to clients signal that all clients are done downloading
	for (int i = 1; i < numtasks; i++) {
		MPI_Send(&dummy, HASH_SIZE, MPI_CHAR, i, HASH_REQ_TAG, MPI_COMM_WORLD);
	}
}

void peer(int numtasks, int rank) {
	pthread_t download_thread;
	pthread_t upload_thread;
	void *status;
	int r;

	map<string, vector<string>> seed_files;
	vector<string> leech_files;

	ifstream fin("in" + to_string(rank) + ".txt");

	// read number of seed files
	int num_files;
	fin >> num_files;

	// send number of seed files to tracker
	MPI_Send(&num_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

	for (int i = 0; i < num_files; i++) {
		string filename;
		fin >> filename;

		seed_files[filename] = vector<string>();

		int num_segments;
		fin >> num_segments;

		// for each file, send filename and number of segments to tracker
		MPI_Send(filename.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
		MPI_Send(&num_segments, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

		for (int j = 0; j < num_segments; j++) {
			string hash;
			fin >> hash;
			seed_files[filename].push_back(hash);

			// for each segment, send hash to tracker
			MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
		}
	}

	// read number of leech files
	fin >> num_files;

	for (int i = 0; i < num_files; i++) {
		string filename;
		fin >> filename;
		leech_files.push_back(filename);
	}

	fin.close();

	// receive start signal from tracker
	MPI_Bcast(&r, 1, MPI_INT, TRACKER_RANK, MPI_COMM_WORLD);

	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex, NULL);

	args_struct args;
	args.rank = rank;
	args.point1 = &leech_files;
	args.point2 = &seed_files;
	args.mutex = &mutex;

	r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &args);
	if (r) {
		printf("Eroare la crearea thread-ului de download\n");
		exit(-1);
	}

	r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &args);
	if (r) {
		printf("Eroare la crearea thread-ului de upload\n");
		exit(-1);
	}

	r = pthread_join(download_thread, &status);
	if (r) {
		printf("Eroare la asteptarea thread-ului de download\n");
		exit(-1);
	}

	r = pthread_join(upload_thread, &status);
	if (r) {
		printf("Eroare la asteptarea thread-ului de upload\n");
		exit(-1);
	}
}

int main (int argc, char *argv[]) {
	int numtasks, rank;
 
	int provided;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (provided < MPI_THREAD_MULTIPLE) {
		fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
		exit(-1);
	}
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == TRACKER_RANK) {
		tracker(numtasks, rank);
	} else {
		peer(numtasks, rank);
	}

	MPI_Finalize();
}
