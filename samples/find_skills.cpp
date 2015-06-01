/*
The MIT License (MIT)

Copyright (c) 2015 Robert Lindsey

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef FIND_SKILLS_CPP
#define FIND_SKILLS_CPP

#include "common.hpp"
#include "MixtureWCRP.hpp"

using namespace std;


// reads a tab delimited file with the columns: student id, item id, skill id, recall success
// all ids are assumed to start at 0 and be contiguous
void load_dataset(const char * filename, vector<size_t> & provided_skill_assignments, vector< vector<bool> > & recall_sequences, vector< vector<size_t> > & item_sequences, size_t & num_students, size_t & num_items, size_t & num_skills) {

	num_students=0, num_items=0, num_skills=0;
	size_t student, item, skill, recall;

	ifstream in(filename);
	if (!in.is_open()) { 
		cerr << "couldn't open " << string(filename) << endl;
		exit(EXIT_FAILURE);
	}
	
	// figure out how many students, items, and skills there are
	while (in >> student >> item >> skill >> recall) {
		num_students = max(student+1, num_students);
		num_items = max(item+1, num_items);
		num_skills = max(skill+1, num_skills);
	}
	in.close();
	cout << "dataset has " << num_students << " students, " << num_items << " items, and " << num_skills << " expert-provided skills" << endl;

	// initialize
	provided_skill_assignments.resize(num_items, -1); // skill_assignments[item index] = skill index
	recall_sequences.resize(num_students);
	item_sequences.resize(num_students);

	// read the dataset
	in.open(filename);
	while (in >> student >> item >> skill >> recall) {
		recall_sequences[student].push_back(recall);
		item_sequences[student].push_back(item);
		provided_skill_assignments[item] = skill;
	}
	in.close();
}


int main(int argc, char ** argv) {

	namespace po = boost::program_options;

	string datafile, savefile;
	int tmp_num_iterations, tmp_burn, tmp_num_subsamples;
	double init_beta, init_alpha_prime;
	bool infer_beta, infer_alpha_prime, map_estimate;

	// parse the command line arguments
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "print help message")
		("datafile", po::value<string>(&datafile), "(required) file containing the student recall data")
		("savefile", po::value<string>(&savefile), "(required) file to put the skill assignments")
		("map_estimate", "(optional) save the MAP skill assignments instead of all sampled skill assignments")
		("num_iterations", po::value<int>(&tmp_num_iterations)->default_value(200), "(optional) number of iterations to run. if you're not sure how to set it, use a large value")
		("burn", po::value<int>(&tmp_burn)->default_value(100), "(optional) number of iterations to discard. if you're not sure how to set it, use a large value (less than num_iterations)")
		("fix_alpha_prime", po::value<double>(&init_alpha_prime), "(optional) fix alpha' at the provided value instead of letting the model try to estimate it")
		("fix_beta", po::value<double>(&init_beta), "(optional) fix beta at the provided value instead of letting the model try to estimate it")
		("num_subsamples", po::value<int>(&tmp_num_subsamples)->default_value(2000), "number of samples to use when approximating marginal likelihood of new skills")
	;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (argc == 1 || vm.count("help")) {
		cout << desc << endl;
		return EXIT_SUCCESS;
	}

	map_estimate = vm.count("map_estimate");

	if (vm.count("fix_alpha_prime")) {
		assert(init_alpha_prime >= 0);
		infer_alpha_prime = false;
		cout << "alpha' will be fixed at " << init_alpha_prime << endl;
	}
	else {
		init_alpha_prime = -1;
		infer_alpha_prime = true;
	}
	
	size_t num_iterations = (size_t) tmp_num_iterations;
	size_t burn = (size_t) tmp_burn;
	size_t num_subsamples = (size_t) tmp_num_subsamples;

	Random * generator = new Random(time(NULL));

	if (vm.count("fix_beta")) {
		assert(init_beta >= 0 && init_beta <= 1);
		infer_beta = false;
		cout << "beta will be fixed at " << init_beta << endl;
	}
	else {
		init_beta = .5; // arbitrary starting value < 1
		infer_beta = true;
	}

	assert(num_iterations >= 0);
	assert(num_iterations > burn);

	// load the dataset
	vector<size_t> provided_skill_assignments;
	vector< vector<bool> > recall_sequences; // recall_sequences[student][trial # i]  = recall success or failure of the ith trial we have for the student
	vector< vector<size_t> > item_sequences; // item_sequences[student][trial # i] = item corresponding to the ith trial we have for the student
	size_t num_students, num_items, num_skills_dataset;
	load_dataset(datafile.c_str(), provided_skill_assignments, recall_sequences, item_sequences, num_students, num_items, num_skills_dataset);
	assert(num_students > 0 && num_items > 0);

	// we'll let the model use all the students as training data: 
	set<size_t> train_students; 
	for (size_t s = 0; s < num_students; s++) train_students.insert(s);
	
	// create the model
	MixtureWCRP model(generator, train_students, recall_sequences, item_sequences, provided_skill_assignments, init_beta, init_alpha_prime, num_students, num_items, num_subsamples);

	// run the sampler
	model.run_mcmc(num_iterations, burn, infer_beta, infer_alpha_prime);
	
	ofstream out_skills(savefile.c_str(), ofstream::out);
	if (map_estimate) { // save the most likely skill assignment
		vector<size_t> map_estimate = model.get_most_likely_skill_assignments();
		assert(map_estimate.size() == num_items);
		for (size_t item = 0; item < num_items; item++) {
			out_skills << map_estimate.at(item);
			if (item == num_items - 1) out_skills << endl;
			else out_skills << " ";
		}
	}
	else { // save all sampled skill assignments 
		vector< vector<size_t> > skill_samples = model.get_skill_assignments();
		assert(!skill_samples.empty());
		for (size_t sample = 0; sample < skill_samples.size(); sample++) {
			assert(skill_samples.at(sample).size() == num_items);
			for (size_t item = 0; item < num_items; item++) {
				out_skills << skill_samples.at(sample).at(item);
				if (item == num_items - 1) out_skills << endl;
				else out_skills << " ";
			}
		}
	}

	delete generator;
	return EXIT_SUCCESS;
}

#endif