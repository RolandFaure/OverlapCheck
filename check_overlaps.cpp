#include "check_overlaps.h"
#include "edlib.h"
// #include "spoa/spoa.hpp"
#include "cluster_graph.h"

#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <fstream>

#include "robin_hood.h"
#include "input_output.h"

using std::string;
using std::cout;
using std::endl;
using std::list;
using std::vector;
using std::min;
using std::max;
using std::begin;
using std::end;
using std::pair;
using std::make_pair;

//definition of a small struct that will be useful later
struct distPart{
    float distance = 1;
    short phased = 1;
};
bool comp (distPart i, distPart j){
    return i.distance < j.distance;
}

//input : the set of all overlaps and the backbone reads
//output : a partition for all backbone reads. All reads also have updated backbone_seqs, i.e. the list of backbone reads they are leaning on
void checkOverlaps(std::vector <Read> &allreads, std::vector <Overlap> &allOverlaps, std::vector<unsigned long int> &backbones_reads, std::unordered_map <unsigned long int ,vector<int>> &partitions, bool assemble_on_assembly) {

    //main loop : for each backbone read, build MSA (Multiple Sequence Alignment) and separate the reads
    int index = 0;
    for (unsigned long int read : backbones_reads){
        
        if (allreads[read].neighbors_.size() > 20 && allreads[read].name == "edge_1"){

            cout << "Looking at backbone read number " << index << " out of " << backbones_reads.size() << " (" << allreads[read].name << ")" << endl;

            vector<Column> snps;  //vector containing list of position, with SNPs at each position
            //first build an MSA
            cout << "Generating MSA" << endl;
            Partition truePar; //for debugging
            float meanDistance = generate_msa(read, allOverlaps, allreads, snps, partitions.size(), truePar, assemble_on_assembly);

            //then separate the MSA
            cout << "Separating reads" << endl;
            auto par = separate_reads(read, allOverlaps, allreads, snps, meanDistance, allreads[read].neighbors_.size()+1-int(assemble_on_assembly));
            cout << "True partition : " << endl;
            truePar.print();
            
            // auto par = truePar.getPartition();//DEBUG
            for (auto i = 0 ; i < par.size() ; i++) {par[i]++; }
            cout << "Proposed partition : " << endl;
            for (auto i = 0 ; i < par.size() ; i++){cout << par[i];}cout << endl;
            cout << endl;
            
            partitions[read] = par;
    
        }
        index++;
    }
}

//input: a read with all its neighbor
//outputs : the precise alignment of all reads against input read in the form of matrix snps, return the mean editDistance/lengthOfAlginment
float generate_msa(long int read, std::vector <Overlap> &allOverlaps, std::vector <Read> &allreads, std::vector<Column> &snps, int backboneReadIndex, Partition &truePar, bool assemble_on_assembly){

    // cout << "neighbors of read " << allreads[read].name << " : " << allreads[read].sequence_.str() << endl;
    //go through the neighbors of the backbone read and align it

    //keep count of the distance between two reads to know the mean distance
    float totalDistance = 0;
    double totalLengthOfAlignment = 0;
    
    //first enter the sequence of the read in snps, if the backbone read is a read, not if it's a contig

    // if (!assemble_on_assembly){
    //     string readSequence = allreads[read].sequence_.str();
    //     for (auto i = 0 ; i < readSequence.size() ; i++){
    //         snps[i][snps[i].size()-1] = readSequence[i];
    //     }
    // }

    //to remember on what backbone the backbone read is leaning -> itself
    allreads[read].new_backbone(make_pair(backboneReadIndex, allreads[read].neighbors_.size()), allreads[read].neighbors_.size()+1);
    string read_str = allreads[read].sequence_.str();

    //small loop to compute truePartition DEBUG
    Column truePartition; //for debugging
    // vector <char> truePart;
    for (auto n = 0 ; n<allreads[read].neighbors_.size() ; n++){

        long int neighbor = allreads[read].neighbors_[n];
        Overlap overlap = allOverlaps[neighbor];
        truePartition.readIdxs.push_back(n);
        if (overlap.sequence1 == read){
            // truePart.push_back(allreads[overlap.sequence2].name[1]);
            if (allreads[overlap.sequence2].name[1] == '1'){
                truePartition.content.push_back('A');
            }
            else{
                truePartition.content.push_back('C');
            }
            // cout << "name : " << allreads[overlap.sequence2].name << " " << allreads[overlap.sequence2].name[1] << " " << truePartition[truePartition.size()-1] << endl;
        }
        else{
            // truePart.push_back(allreads[overlap.sequence1].name[1]);
            if (allreads[overlap.sequence1].name[1] == '1'){
                truePartition.content.push_back('C');
            }
            else{
                truePartition.content.push_back('A');
            }
            // cout << "name : " << allreads[overlap.sequence1].name << " " << allreads[overlap.sequence1].name[1] << " " << truePartition[truePartition.size()-1] << endl;
        }    
    }
    //do not forget the last read itself
    // truePart.push_back(allreads[read].name[1]);
    if (allreads[read].name[1] == '1'){
        truePartition.content.push_back('A');
    }
    else{
        truePartition.content.push_back('C');
    }

    // string fileOut = "/home/rfaure/Documents/these/overlap_filtering/species/Escherichia/triploid/answer_"+std::to_string(read)+".tsv";
    // std::ofstream out(fileOut);
    // for (auto c : truePart){out<<c;}out.close();
    // cout << "name : " << allreads[read].name << " " << allreads[read].name[1] << " " << truePartition[truePartition.size()-1] << endl;
    truePar = Partition(truePartition, 0);

    // /* compute only true partition

    //now mark down on which backbone read those reads are leaning
    //in the same loop, inventoriate all the polishing reads
    vector <string> polishingReads;
    vector <pair <int,int>> positionOfReads; //position of polishing reads on the consensus
    for (auto n = 0 ; n<allreads[read].neighbors_.size() ; n++){
        long int neighbor = allreads[read].neighbors_[n];
        Overlap overlap = allOverlaps[neighbor];
        // cout << "overlap : " << overlap.position_1_1 << " " << overlap.position_1_2 << " " << overlap.position_2_1 << " "
        //     << overlap.position_2_2 << endl;
        if (overlap.sequence1 == read){
            // cout << "Neighbor of " << allreads[overlap.sequence1].name << " : " << allreads[overlap.sequence2].name << endl;
            allreads[overlap.sequence2].new_backbone(make_pair(backboneReadIndex,n), allreads[read].neighbors_.size()+1);
            if (overlap.strand){
                polishingReads.push_back(allreads[overlap.sequence2].sequence_.subseq(overlap.position_2_1, overlap.position_2_2-overlap.position_2_1).str());
                positionOfReads.push_back(make_pair(overlap.position_1_1, overlap.position_1_2));
            }
            else{
                polishingReads.push_back(allreads[overlap.sequence2].sequence_.subseq(overlap.position_2_1, overlap.position_2_2-overlap.position_2_1).reverse_complement().str());
                positionOfReads.push_back(make_pair(overlap.position_1_1, overlap.position_1_2));
            }
        }
        else {
            // cout << "Neighbor of " << allreads[overlap.sequence2].name << " : " << allreads[overlap.sequence1].name << endl;
            allreads[overlap.sequence1].new_backbone(make_pair(backboneReadIndex,n), allreads[read].neighbors_.size()+1);
            if (overlap.strand){
                polishingReads.push_back(allreads[overlap.sequence1].sequence_.subseq(overlap.position_1_1, overlap.position_1_2-overlap.position_1_1).str());
                positionOfReads.push_back(make_pair(overlap.position_2_1, overlap.position_2_2));
            }
            else{
                polishingReads.push_back(allreads[overlap.sequence1].sequence_.subseq(overlap.position_1_1, overlap.position_1_2-overlap.position_1_1).reverse_complement().str());
                positionOfReads.push_back(make_pair(overlap.position_2_1, overlap.position_2_2));
            }
        }
    }

    string consensus = consensus_reads(read_str , polishingReads);
    cout << "Done building a consensus of the backbone" << endl;

    if (!assemble_on_assembly){
        polishingReads.push_back(read_str); //now we'll use polishingReads as the list of reads aligning on the consensus
    }
    //snps = vector<vector<char>>(consensus.size(), vector<char>(polishingReads.size(), '?'));
    snps = vector<Column>(consensus.size());

    //while all the alignments are computed, build the positions
    robin_hood::unordered_map<int, int> insertionPos;
    vector<int> numberOfInsertionsHere (consensus.size()+1, 0);

    for (auto n = 0 ; n < polishingReads.size() ; n++){

        cout << "Aligned " << n << " reads out of " << allreads[read].neighbors_.size() << " on the backbone\r";

        EdlibAlignResult result = edlibAlign(polishingReads[n].c_str(), polishingReads[n].size(),
                                    consensus.substr(positionOfReads[n].first, positionOfReads[n].second-positionOfReads[n].first).c_str(),
                                    positionOfReads[n].second-positionOfReads[n].first,
                                    edlibNewAlignConfig(-1, EDLIB_MODE_HW, EDLIB_TASK_PATH, NULL, 0));

    
        totalLengthOfAlignment += result.alignmentLength;
        totalDistance += result.editDistance;
        // cout << "Alignment distance : " << float(result.editDistance)/result.alignmentLength << endl;

        // if (n == 10) {break;}

        //a loop going through the CIGAR and modifyning snps
        char moveCodeToChar[] = {'=', 'I', 'D', 'X'};
        int indexQuery = result.startLocations[0]+positionOfReads[n].first; //query corresponds to read
        int indexTarget = 0; //target corresponds to the consensus
        int numberOfInsertionsThere = 0;

        //cout << "beginning of query : " << indexQuery << " " << consensus.size() << " " << snps.size() << " " << result.alignmentLength << endl;
        
        // if (n == 169){
        // for (int i = 0; i < result.alignmentLength; i++){cout << moveCodeToChar[result.alignment[i]];} cout << endl;}

        
        for (int i = 0; i < result.alignmentLength; i++) {

            if (moveCodeToChar[result.alignment[i]] == '=' || moveCodeToChar[result.alignment[i]] == 'X'){

                //fill inserted columns with '-' just before that position
                for (int ins = numberOfInsertionsThere ; ins < numberOfInsertionsHere[indexQuery] ; ins++){ //in these positions, insert '-' instead of '?'
                    snps[insertionPos[10000*indexQuery+ins]].readIdxs.push_back(n);
                    snps[insertionPos[10000*indexQuery+ins]].content.push_back('-');
                }

                snps[indexQuery].readIdxs.push_back(n);
                snps[indexQuery].content.push_back(polishingReads[n][indexTarget]);
                indexQuery++;
                indexTarget++;
                numberOfInsertionsThere = 0;
            }
            else if (moveCodeToChar[result.alignment[i]] == 'D'){

                //fill inserted columns with '-' just before that position
                for (int ins = numberOfInsertionsThere ; ins < numberOfInsertionsHere[indexQuery] ; ins++){ //in these positions, insert '-' instead of '?'
                    snps[insertionPos[10000*indexQuery+ins]].readIdxs.push_back(n);
                    snps[insertionPos[10000*indexQuery+ins]].content.push_back('-');
                }

                snps[indexQuery].readIdxs.push_back(n);
                snps[indexQuery].content.push_back('-');
                indexQuery++;
                numberOfInsertionsThere = 0;
            }
            else if (moveCodeToChar[result.alignment[i]] == 'I'){ //hardest one
                if (numberOfInsertionsHere[indexQuery] < 9999 && indexQuery > positionOfReads[n].first) {

                    if (numberOfInsertionsThere >= numberOfInsertionsHere[indexQuery]) { //i.e. this is a new column
                        insertionPos[10000*indexQuery+numberOfInsertionsHere[indexQuery]] = snps.size();
                        numberOfInsertionsHere[indexQuery] += 1;
                        
                        Column newInsertedPos;
                        newInsertedPos.readIdxs = snps[indexQuery-1].readIdxs;    
                        newInsertedPos.content = vector<char>(snps[indexQuery-1].content.size() , '-'); 
                        newInsertedPos.content[newInsertedPos.content.size()-1] = polishingReads[n][indexTarget];
                        snps.push_back(newInsertedPos);
                    }
                    else{
                        snps[insertionPos[10000*indexQuery+numberOfInsertionsThere]].readIdxs.push_back(n);
                        snps[insertionPos[10000*indexQuery+numberOfInsertionsThere]].content.push_back(polishingReads[n][indexTarget]);
                    }
                    numberOfInsertionsThere ++;
                }
                indexTarget++;
            }
        }

        // cout << "index target : " << indexTarget<< endl;
        // cout << "index query : " << indexQuery << endl;
        // cout << "consensus length : " << consensus.size() << endl;
        
        edlibFreeAlignResult(result);
    }
    
    //print snps (just for debugging)
    // int step = 1;
    // int numberOfReads = 10;
    // int start = 1011;
    // int end = 1060;
    // vector<string> reads (numberOfReads);
    // string cons = "";
    // for (unsigned short i = start ; i < end; i++){
        
    //     for (short n = 0 ; n < numberOfReads*step ; n+= step){
    //         char c = '?';
    //         int ri = 0;
    //         for (auto r : snps[i].readIdxs){
    //             if (r == n){
    //                 c = snps[i].content[ri];
    //             }
    //             ri ++;
    //         }
    //         reads[n/step] += c;
    //     }
    //     // for (short insert = 0 ; insert < min(9999,numberOfInsertionsHere[i]) ; insert++ ){
    //     //     int snpidx = insertionPos[10000*i+insert];
    //     //     for (short n = 0 ; n < numberOfReads*step ; n+= step){
    //     //         char c = '?';
    //     //         int ri = 0;
    //     //         for (auto r : snps[snpidx].readIdxs){
    //     //             if (r == n){
    //     //                 c = snps[snpidx].content[ri];
    //     //             }
    //     //             ri ++;
    //     //         }
    //     //         reads[n/step] += c;
    //     //     }
    //     // }
    // }
    // cout << "Here are the aligned reads : " << endl;
    // int index = 0;
    // for (auto neighbor : reads){
    //     if (neighbor[0] != '?'){
    //         cout << neighbor << " " << index  << endl;
    //     }
    //     index++;
    // }
    // cout << consensus.substr(start, end-start) << endl;

    // cout << "meanDistance : " << totalDistance/totalLengthOfAlignment << endl;
    return totalDistance/totalLengthOfAlignment;

    //*/
}

//input : a backbone read with a list of all reads aligning on it
//output : a polished backbone read
//function using Racon to consensus all reads
string consensus_reads(string &backbone, vector <string> &polishingReads){
    
    system("mkdir tmp/ 2> trash.txt");
    std::ofstream outseq("tmp/unpolished.fasta");
    outseq << ">seq\n" << backbone;
    outseq.close();

    std::ofstream polishseqs("tmp/reads.fasta");
    for (int read =0 ; read < polishingReads.size() ; read++){
        polishseqs << ">read"+std::to_string(read)+"\n" << polishingReads[read] << "\n";
    }
    polishseqs.close();

    system("minimap2 -t 1 -x map-ont tmp/unpolished.fasta tmp/reads.fasta > tmp/mapped.paf 2>tmp/trash.txt");
    system("racon -t 1 tmp/reads.fasta tmp/mapped.paf tmp/unpolished.fasta > tmp/polished.fasta 2>tmp/trash.txt");

    std::ifstream polishedRead("tmp/polished.fasta");
    string line;
    string consensus;
    while(getline(polishedRead, line)){
        if (line[0] != '>'){
            consensus = line; 
        }
    }

    if (consensus == ""){ //that's typically if there are too few reads to consensus
        return backbone;
    }


    //racon tends to drop the ends of the sequence, so attach them back.
    //This is an adaptation in C++ of a Minipolish (Ryan Wick) code snippet 
    auto before_size = min(size_t(500), backbone.size());
    auto after_size = min(size_t(250), consensus.size());

    // Do the alignment for the beginning of the sequence.
    string before_start = backbone.substr(0,before_size);
    string after_start = consensus.substr(0,after_size);

    EdlibAlignResult result = edlibAlign(after_start.c_str(), after_start.size(), before_start.c_str(), before_start.size(),
                                        edlibNewAlignConfig(-1, EDLIB_MODE_HW, EDLIB_TASK_PATH, NULL, 0));


    int start_pos = result.startLocations[0];
    string additional_start_seq = before_start.substr(0, start_pos);

    edlibFreeAlignResult(result);


    // And do the alignment for the end of the sequence.
    string before_end = backbone.substr(backbone.size()-before_size, before_size);
    string after_end = consensus.substr(consensus.size()-after_size , after_size);

    result = edlibAlign(after_start.c_str(), after_start.size(), before_start.c_str(), before_start.size(),
                                        edlibNewAlignConfig(-1, EDLIB_MODE_HW, EDLIB_TASK_PATH, NULL, 0));

    int end_pos = result.endLocations[0]+1;
    string additional_end_seq = before_end.substr(end_pos , before_end.size()-end_pos);
    edlibFreeAlignResult(result);
    
    // cout << "consensus : " << endl << (additional_start_seq + consensus + additional_end_seq).substr(100,150) << endl;
    // cout << backbone.substr(100,150) << endl;
    
    return additional_start_seq + consensus + additional_end_seq;

}

//input : a set of reads aligned to read in matrix snps
//output : reads separated by their region of origin
vector<int> separate_reads(long int read, std::vector <Overlap> &allOverlaps, std::vector <Read> &allreads, std::vector<Column> &snps, float meanDistance, int numberOfReads){

    /*
    The null model is described as uniform error rate -> binomial error distribution on one position
    meanDistance ~= 2*sequencingErrorRate (e) at most
    suspicious positions have p-value < 0.05, i.e. more errors than n*e + 3*sqrt(n*e*(1-e))
    */

    robin_hood::unordered_map<char, short> bases2content;
    bases2content['A'] = 0;
    bases2content['C'] = 1; 
    bases2content['G'] = 2;
    bases2content['T'] = 3;
    bases2content['-'] = 4;
    bases2content['?'] = 5;

    vector<Partition> partitions; //list of all partitions of the reads, with the number of times each occurs

    int numberOfSuspectPostion = 0;
    int numberOfNeighbors = 0;

    vector<vector<distPart>> distanceBetweenPartitions;
    vector<size_t> suspectPostitions;

    for (int position = 0 ; position < snps.size() ; position++){  

        //first look at the position to see if it is suspect
        int content [5] = {0,0,0,0,0}; //item 0 for A, 1 for C, 2 for G, 3 for T, 4 for -
        int numberOfReads = 0;
        for (short n = 0 ; n < snps[position].content.size() ; n++){
                char base = snps[position].content[n];
                if (base != '?' && bases2content.contains(base)){
                    content[bases2content[base]] += 1;
                    numberOfReads += 1;
                }
        }

        float threshold = 1 + numberOfReads*meanDistance/2 + 3*sqrt(numberOfReads*meanDistance/2*(1-meanDistance/2));
        //threshold = 3; //DEBUG
        if (*std::max_element(content, content+5) < numberOfReads-threshold){ //this position is suspect
            //cout << threshold << " " << position << " ;bases : " << content[0] << " " << content[1] << " " << content[2] << " " << content[3] << " " << content[4] << endl;
            suspectPostitions.push_back(position);
            //go through the partitions to see if this suspicious position looks like smt we've seen before
            vector<distPart> distances (distanceBetweenPartitions.size());
            bool found = false;
            for (auto p = 0 ; p < partitions.size() ; p++){

                distancePartition dis = distance(partitions[p], snps[position]);
                auto comparable = min(dis.n00,dis.n11) + dis.n01 + dis.n10;
                // if (comparable > 10){
                //     cout << "comparable : " << float(dis.n01+dis.n10)/(dis.n00+dis.n11+dis.n01+dis.n10) << " " << dis.augmented<< " mean distance : " << meanDistance << endl;
                //     partitions[p].print();
                //     Partition(snps[position]).print();
                    
                // }
                if (float(dis.n01+dis.n10)/(dis.n00+dis.n11+dis.n01+dis.n10) <= meanDistance && dis.augmented && comparable > min(10.0, 0.3*numberOfReads)){
                    // if (p == 984){
                    //     Partition(snps[position]).print();
                    // }
                    int pos = -1;
                    if (position < allreads[read].size()){
                        pos = position;
                    }
                    partitions[p].augmentPartition(dis.partition_to_augment, pos);

                    break;
                }
                if (comparable > 5){
                    //distances[p].distance = max(float(0), (10-dis.chisquare)/10) ;
                    distances[p].distance = float(dis.n01+dis.n10)/(dis.n00 + dis.n11 + dis.n01 + dis.n10);
                    distances[p].phased = dis.phased;
                }
                else{
                    distances[p].distance = 1;
                }
            }

            if (!found){    
                distanceBetweenPartitions.push_back(distances);
                partitions.push_back(Partition(snps[position], position)); 
            }
            
            numberOfSuspectPostion += 1;

            //two suspect positions next to each other can be artificially correlated through alignement artefacts
            position += 5;
        }
    }

    //for debugging only
    //outputMatrix(snps, suspectPostitions, std::to_string(read));


    if (partitions.size() == 0){ //there are no position of interest
        return vector<int> (allreads[read].neighbors_.size(), 1);
    }

    // cout << "Outputting the graph" << endl;

    /* looking at the graph
    //square the distanceBetweenPartions matrix (which is a triangle matrix until now)

    for (int i = 0 ; i < distanceBetweenPartitions.size() ; i++){
        distPart diag;
        distanceBetweenPartitions[i].push_back(diag); //that is the diagonal
        while(distanceBetweenPartitions[i].size() < distanceBetweenPartitions.size()){
            distanceBetweenPartitions[i].push_back(diag);
        }
    }
    for (int i = 0 ; i < distanceBetweenPartitions.size() ; i++){
       for (int j = i+1 ; j < distanceBetweenPartitions.size() ; j++){
            distanceBetweenPartitions[i][j].distance = distanceBetweenPartitions[j][i].distance;
        }
    }

    // build the adjacency matrix
    vector<vector <float>> adj (distanceBetweenPartitions.size(), vector<float> (distanceBetweenPartitions.size(), 0));
    //each node keeps only the links to very close elements
    int numberOfNeighborsKept = 5;
    for (auto i = 0 ; i < distanceBetweenPartitions.size() ; i++){
        if (distanceBetweenPartitions[i].size() > numberOfNeighborsKept){
            vector <distPart> minElements (numberOfNeighborsKept);
            std::partial_sort_copy(distanceBetweenPartitions[i].begin(), distanceBetweenPartitions[i].end(), minElements.begin(), minElements.end(), comp);
            float maxDiff = minElements[numberOfNeighborsKept-1].distance;

            //cout << "maxdiff of partition " << i << " : " << maxDiff << endl;
            if (i == 1){
                // cout << "1 : " << endl;
                // for(auto j:adj[i]){cout<<j << " ";}
                // cout << endl;
            }
            
            int numberOf1s = 0;
            for (int j = 0 ; j < distanceBetweenPartitions[i].size() ; j++){
                if (distanceBetweenPartitions[i][j].distance > maxDiff || distanceBetweenPartitions[i][j].distance == 1 || numberOf1s >= numberOfNeighborsKept){
                    
                }
                else {
                    if (distanceBetweenPartitions[i][j].distance < meanDistance){
                        // cout << "distance : " << distanceBetweenPartitions[i][j].distance << endl;
                        adj[i][j] = 1;
                        adj[i][j] = 1;
                        numberOf1s++;
                    }
                }
            }
        }
    }

    */

   /* to look at the graph

    vector<int> clusters (partitions.size());

    cluster_graph_chinese_whispers(adj, clusters);
    outputGraph(adj, clusters, "graph.gdf");

    //filter out too small clusters
    vector <int> sizeOfCluster;
    for (auto p = 0 ; p < partitions.size() ; p++){
        while (clusters[p] >= sizeOfCluster.size() ){ 
            sizeOfCluster.push_back(0);
        }
        sizeOfCluster[clusters[p]] += 1;
    }
    vector<Partition> listOfFinalPartitionsdebug(sizeOfCluster.size(), Partition(partitions[0].size()));

    //now merge each cluster in one partition
    for (auto p = 0 ; p < partitions.size() ; p++){

        if (sizeOfCluster[clusters[p]] > 3){
            // if (sizeOfCluster[clusters[p]] == 41){
            //     partitions[p].print();
            // }
            listOfFinalPartitionsdebug[clusters[p]].mergePartition(partitions[p]);
        }
    }

    //filter out empty partitions
    vector<Partition> listOfFinalPartitions2;
    for (auto p : listOfFinalPartitionsdebug){
        if (p.number()> 0 && p.isInformative(meanDistance/2, true)) {
            listOfFinalPartitions2.push_back(p);
        }
    }
    listOfFinalPartitionsdebug = listOfFinalPartitions2;

    for (auto p : listOfFinalPartitionsdebug){
        cout << "final : " << endl;
        p.print();
    }

    */
    
    float threshold =  max(4.0, min(0.01*numberOfSuspectPostion, 0.001*snps.size()));

    vector<Partition> listOfFinalPartitions;
    for (auto p1 = 0 ; p1 < partitions.size() ; p1++){

        // if (partitions[p1].number() > 2){
        //     cout << "non informative partition : "  << threshold << " " << numberOfSuspectPostion << " " << snps.size()<< endl;
        //     partitions[p1].print();
        // }
        
        if (partitions[p1].number() > threshold && partitions[p1].isInformative(meanDistance/2, true)){

            // cout << "informative partition 2 : " << endl;
            // partitions[p1].print();

            bool different = true;
            
            for (auto p2 = 0 ; p2 < listOfFinalPartitions.size() ; p2++){

                distancePartition dis = distance(listOfFinalPartitions[p2], partitions[p1], 2);
                if (dis.augmented){
                    auto chi = computeChiSquare(dis);
                    if (chi > 9){
                        listOfFinalPartitions[p2].mergePartition(partitions[p1], dis.phased);
                        different = false;
                    }
                }
            }
            
            if (different){
                listOfFinalPartitions.push_back(partitions[p1]);
            }
        }
    }

    //now we have the list of final partitions : there may be several, especially if there are more than two copies

    if (listOfFinalPartitions.size() == 0){
        return vector <int> (allreads[read].neighbors_.size(), 0);
    }

    vector<Partition> listOfFinalPartitionsTrimmed = select_partitions(listOfFinalPartitions, numberOfReads);


    //now aggregate all those binary partitions in one final partition. There could be up to 2^numberBinaryPartitions final groups
    vector<int> threadedClusters = threadHaplotypes2(listOfFinalPartitionsTrimmed, numberOfReads);

    // cout << "threaded clusters : " << endl;
    // for (auto i = 0 ; i < threadedClusters.size() ; i++){cout << threadedClusters[i];}cout << endl;

    // //rescue reads that have not been assigned to a cluster
    // vector<int> finalClusters = rescue_reads(threadedClusters, snps, suspectPostitions);

    // return finalClusters;
    return threadedClusters;
}

//input : one partition and one list of chars
//output : is the list of chars close to the partition ? If so, augment the partition. Return the chi-square of the difference in bases
distancePartition distance(Partition &par1, Column &par2){

    /*
    when computing the distance, there is not 5 letters but 2 : the two alleles, which are the two most frequent letters
    */
    distancePartition res;
    res.augmented = true;
    vector <int> idxs1 = par1.getReads();
    vector<short> part1 = par1.getPartition();
    vector<float> confs1 = par1.getConfidence();

    robin_hood::unordered_flat_map<char, short> bases2content;
    bases2content['A'] = 0;
    bases2content['C'] = 1; 
    bases2content['G'] = 2;
    bases2content['T'] = 3;
    bases2content['-'] = 4;
    bases2content['?'] = 5;
    
    int content2 [5] = {0,0,0,0,0}; //item 0 for A, 1 for C, 2 for G, 3 for T, 4 for *, 5 for '?'
    float numberOfBases = 0;
    auto it1 = idxs1.begin();
    int n2 = 0;
    auto it2 = par2.readIdxs.begin();
    while (it1 != idxs1.end() && it2 != par2.readIdxs.end()){
        if (*it1 == *it2){
            numberOfBases++;
            content2[bases2content[par2.content[n2]]] += 1;
            ++it1;
            ++it2;
            n2++;
        }
        else if (*it2 > *it1){
            ++it1;
        }
        else{
            ++it2;
            n2++;
        }
    }

    if (numberOfBases < 10){ //not comparable
        res.n00 = 0;
        res.n01 = 0;
        res.n10 = 0;
        res.n11 = 0;
        res.augmented = false;
        return res;
    }

    //determine first and second most frequent bases in par2
    char mostFrequent2 = 'A';
    int maxFrequence2 = content2[0];
    char secondFrequent2 = 'C';
    int secondFrequence2 = content2[1];
    if (content2[0] < content2[1]){
        mostFrequent2 = 'C';
        maxFrequence2 = content2[1];
        secondFrequent2 = 'A';
        secondFrequence2 = content2[0];
    }
    for (auto i = 2 ; i < 5 ; i++){
        if (content2[i] > maxFrequence2){
            secondFrequence2 = maxFrequence2;
            secondFrequent2 = mostFrequent2;
            maxFrequence2 = content2[i];
            mostFrequent2 = "ACGT-"[i];
        }
        else if (content2[i] > secondFrequence2){
            secondFrequent2 = "ACGT-"[i];
            secondFrequence2 = content2[i];
        }
    }

    // cout << "Two most frequent : " << mostFrequent2 << "," << secondFrequent2 << " : ";
    // for (auto i : par2){cout << i;} cout << endl;

    float scores [2] = {0,0}; //the scores when directing mostFrequent on either mostfrequent2 or secondFrequent2
    float bestScore = 0;
    //remember all types of matches for the chi square test
    int matches00[2] = {0,0};
    int matches01[2] = {0,0};
    int matches10[2] = {0,0};
    int matches11[2] = {0,0};
    Column newPartitions [2];

    newPartitions[0].readIdxs = par2.readIdxs;
    newPartitions[1].readIdxs = par2.readIdxs;
    for (auto c : par2.content){
        if (c == mostFrequent2){
            newPartitions[0].content.push_back('A');
            newPartitions[1].content.push_back('a');
        }
        else if (c == secondFrequent2){
            newPartitions[0].content.push_back('a');
            newPartitions[1].content.push_back('A');
        }
        else{
            newPartitions[0].content.push_back('0');
            newPartitions[1].content.push_back('0');
        }
    }


    int n1 = 0;
    it1 = idxs1.begin();
    n2 = 0;
    it2 = par2.readIdxs.begin();
    while (it1 != idxs1.end() && it2 != par2.readIdxs.end()){

        float conf = 2*confs1[n2]-1;

        if (*it1 == *it2){
            ++it1;
            ++it2;

            if (par2.content[n2] == mostFrequent2){

                if (part1[n1] == 1){
                    scores[0] += conf;
                    scores[1] -= conf;
                    bestScore += conf;
                    matches11[0] += 1;
                    matches10[1] += 1;
                }
                else if (part1[n1] == -1){
                    scores[0] -= conf;
                    scores[1] += conf;
                    bestScore += conf;
                    matches01[0] += 1;
                    matches00[1] += 1;
                }
            }
            else if (par2.content[n2] == secondFrequent2){

                if (part1[n1] == 1){
                    scores[0] -= conf;
                    scores[1] += conf;
                    bestScore += conf;
                    matches10[0] += 1;
                    matches11[1] += 1;
                }
                else if (part1[n1] == -1){
                    scores[0] += conf;
                    scores[1] -= conf;
                    bestScore += conf;
                    matches00[0] += 1;
                    matches01[1] += 1;
                }
            }
            n1++;
            n2++;
        }
        else if (*it2 > *it1){
            ++it1;
            n1++;
        }
        else{
            ++it2;
            n2++;
        }
    }

    //now look at the best scores

    auto maxScore = scores[0];
    auto maxScoreIdx = 0; //can be either 0 or 1
    for (auto i = 0 ; i < 2 ; i++){
        //cout << scores[i] << " , ";
        if (scores[i]>=maxScore){
            maxScore = scores[i];
            maxScoreIdx = i;
        }
    }

    res.n00 = matches00[maxScoreIdx];
    res.n01 = matches01[maxScoreIdx];
    res.n10 = matches10[maxScoreIdx];
    res.n11 = matches11[maxScoreIdx];
    res.score = scores[maxScoreIdx]/bestScore;
    res.phased = -2*maxScoreIdx + 1;
    res.partition_to_augment = newPartitions[maxScoreIdx];
    //cout << "Computing..." << maxScore << " " << par1.size()-res.nonComparable << " " << res.nmismatch << endl;

    return res;
}

//input : a distancePartition 
//output : the chisquare (one degree of freedom)
float computeChiSquare(distancePartition dis){

    int n = dis.n00 + dis.n01 + dis.n10 + dis.n11;
    if (n == 0){
        return 0;
    }
    float pmax1 = float(dis.n10+dis.n11)/n;
    float pmax2 = float(dis.n01+dis.n11)/n;
    //now a few exceptions when chisquare cannot be computed
    if (pmax1*(1-pmax1) == 0 && pmax2*(1-pmax2) == 0){
        return -1;
    }
    if (pmax1*pmax2*(1-pmax1)*(1-pmax2) == 0){ //if there is only one base in one partition, it can't be compared
        return 0;
    }

    // cout << "ps : " << pmax1 << ", " << pmax2 << endl;
    // cout << "expected/obtained : " << dis.n00 << "/"<<(1-pmax1)*(1-pmax2)*n << " ; " << dis.n01 << "/"<<(1-pmax1)*pmax2*n
    // << " ; " << dis.n10 << "/" << pmax1*(1-pmax2)*n << " ; " << dis.n11 << "/" << pmax1*pmax2*n << endl;
    //chi square test with 1 degree of freedom
    float res;
    res = pow((dis.n00-(1-pmax1)*(1-pmax2)*n),2)/((1-pmax1)*(1-pmax2)*n)
        + pow((dis.n01-(1-pmax1)*pmax2*n),2)/((1-pmax1)*pmax2*n)
        + pow((dis.n10-pmax1*(1-pmax2)*n),2)/(pmax1*(1-pmax2)*n)
        + pow((dis.n11-pmax1*pmax2*n),2)/(pmax1*pmax2*n);

    return res;
}

//input : two partitions and thresholds for comparing the partitions
//output : true if the two partitions are the same given the thresholds. In that case, merge partitions into par1
distancePartition distance(Partition &par1, Partition &par2, int threshold_p){
    /*
    Two metrics are used to compare two partition : the chi to see if the two partition correlate when they are small
                                                    the p when the partitions are bigger and you can do probabilities on them
    */

    float chi = 0;

    vector<int> idx1 = par1.getReads();
    vector<int> idx2 = par2.getReads();

    vector<short> part1 = par1.getPartition();
    vector<short> part2 = par2.getPartition();

    vector<int> more1 = par1.getMore();
    vector<int> less1 = par1.getLess();

    vector<int> more2 = par2.getMore();
    vector<int> less2 = par2.getLess();

    int scores [2] = {0,0}; //the scores when directing mostFrequent on either mostfrequent2 or secondFrequent2
    short ndivergentPositions[2] = {0,0}; //number of positions where these two partitions could not have been so different by chance
    //remember all types of matches for the chi square test
    int matches00[2] = {0,0};
    int matches01[2] = {0,0};
    int matches10[2] = {0,0};
    int matches11[2] = {0,0};

    int r1 = 0;
    int r2 = 0;
    while (r1 < idx1.size() && r2 < idx2.size()){
        if (idx1[r1]< idx2[r2]){
            r1++;
        }
        else if (idx2[r2] < idx1[r1]){
            r2++;
        }
        else{

            float threshold1 = 0.5*(more1[r1]+less1[r1]) + 3*sqrt((more1[r1]+less1[r1])*0.5*(1-0.5)); //to check if we deviate significantly from the "random read", that is half of the time in each partition
            float threshold2 = 0.5*(more2[r2]+less2[r2]) + 3*sqrt((more2[r2]+less2[r2])*0.5*(1-0.5)); //to check if we deviate significantly from the "random read", that is half of the time in each partition

            if (part2[r2] == 1){

                if (part1[r1] == 1){
                    scores[0] += 1;
                    scores[1] -= 1;
                    matches11[0] += 1;
                    matches10[1] += 1;

                    //if both positions are certain, this may be bad
                    if (more1[r1] > threshold1 && more2[r2] > threshold2){
                        ndivergentPositions[1] += 1;
                    }
                }
                else if (part1[r1] == -1){
                    scores[0] -= 1;
                    scores[1] += 1;
                    matches01[0] += 1;
                    matches00[1] += 1;

                    //if both positions are certain, this may be bad
                    if (more1[r1] > threshold1 && more2[r2] > threshold2){
                        ndivergentPositions[0] += 1;
                    }
                }
            }
            else if (part2[r2] == -1){

                if (part1[r1] == 1){
                    scores[0] -= 1;
                    scores[1] += 1;
                    matches10[0] += 1;
                    matches11[1] += 1;

                    //if both positions are certain, this may be bad
                    if (more1[r1] > threshold1 && more2[r2] > threshold2){
                        ndivergentPositions[0] += 1;
                    }
                }
                else if (part1[r1] == -1){
                    scores[0] += 1;
                    scores[1] -= 1;
                    matches00[0] += 1;
                    matches01[1] += 1;

                    //if both positions are certain, this may be bad
                    if (more1[r1] > threshold1 && more2[r2] > threshold2){
                        ndivergentPositions[1] += 1;
                    }
                }
            }

            r1++;
            r2++;
        }
    }


    distancePartition res;
    res.augmented = true;

    //check if there are too many unenxplainable positions
    if (ndivergentPositions[0] >= threshold_p && ndivergentPositions[1] >= threshold_p){
        // cout << "Should not merge those two partitions ! " << endl;
        res.augmented = false;
    }

    /*
    now there aren't too many unexplainable postions. 
    However, that could be due to little partitions on which we could not do stats. For those, do a chi-square
    */

    //now look at the best scores

    int maxScore = scores[0];
    int maxScoreIdx = 0; //can be either 0 or 1
    
    if (scores[1] > maxScore){
        maxScore = scores[1];
        maxScoreIdx = 1;
    }

    res.n00 = matches00[maxScoreIdx];
    res.n01 = matches01[maxScoreIdx];
    res.n10 = matches10[maxScoreIdx];
    res.n11 = matches11[maxScoreIdx];
    res.phased = -2*maxScoreIdx + 1; // worth -1 or 1

    return res ;
}

//input : a list of partitions
//output : only the partitions that look very sure of themselves
vector<Partition> select_partitions(vector<Partition> &listOfFinalPartitions, int numberOfReads){
    vector<int> frequenceOfPart (pow(2, listOfFinalPartitions.size()));

    vector<vector<int>> allIdxs;
    for (auto i : listOfFinalPartitions){
        allIdxs.push_back(i.getReads());
    }
    vector<vector<short>> allPartitions;
    for (auto i : listOfFinalPartitions){
        allPartitions.push_back(i.getPartition());
    }
    vector<vector<float>> allConfidences;
    for (auto i : listOfFinalPartitions){
        allConfidences.push_back(i.getConfidence());
    }
    vector<vector<int>> allMores;
    for (auto i : listOfFinalPartitions){
        allMores.push_back(i.getMore());
    }
    vector<vector<int>> allLess;
    for (auto i : listOfFinalPartitions){
        allLess.push_back(i.getLess());
    }

    //as a first step, we'll try to throw away the partitions that do not look very sure of themselves
    /*to to that, we'll look at each read, see if it's well clustered in one partition. 
    If yes, all partitions where it's badly clustered are suspicious
    Indeed, we expect the different errors to compensate each other
    */
    
    vector<bool> trimmedListOfFinalPartitionBool (listOfFinalPartitions.size(), false); //true if a partition is kept, false otherwise

    vector<bool> readsClassified (numberOfReads, false);
    for (int par = 0 ; par < listOfFinalPartitions.size() ; par++){
        int c = 0;
        for (auto read : allIdxs[par]){
            if (allPartitions[par][c] != 0 && allConfidences[par][c] >= 0.7){
                readsClassified[read] = true;
            }
            c++;
        }
    }

    //now we know what reads are not well classified anywhere. Now see if some partitions are just not sure enough of themselves
    //partitions are also expected to be compatibles, thus to correlate wiht each other. If it is not the case, dump the the weakest partition
    for (int par = 0 ; par < listOfFinalPartitions.size() ; par++){

        //because of the ref, the two haplotypes are not strictly equivalent : try to compensate
        int numberOfPartitionnedRead = 0;
        float means [2] = {0,0}; //it's the mean confidencewe have in each haplotype
        float n1 = 0;
        float n0 = 0;
        int c = 0;
        for (auto read : allIdxs[par]){
            if (allPartitions[par][c] != 0){
                numberOfPartitionnedRead++;
                if (allPartitions[par][c] == 1){
                    means[1] += allConfidences[par][c];
                    n1++;
                }
                else if (allPartitions[par][c] == -1){
                    means[0] += allConfidences[par][c];
                    n0++;
                }
            }
            c++;
        }

        //compute the confidence level we must have to be sure (different for both haplotypes because of the bias)
        double errors [2] = {(1-means[0]/n0)/(2-means[0]/n0-means[1]/n1) * 0.6 , (1-means[1]/n1)/(2-means[0]/n0-means[1]/n1) * 0.6 };
        //cout << "the centers are : " << means[0]/n0 << " and " << means[1]/n1 << endl;

        int numberOfUnsureReads = 0;
        c = 0;
        for (auto read : allIdxs[par]){
            if (allPartitions[par][c] != 0 && allConfidences[par][c] < 1-errors[(allPartitions[par][c]+1)/2] && readsClassified[read]){ //oops
                numberOfUnsureReads++;
            }
            c++;
        }

        // cout << "Here is the number of unsure reads " << numberOfUnsureReads << " "<< numberOfPartitionnedRead << endl;

        if (float(numberOfUnsureReads)/numberOfPartitionnedRead < 0.1){ 
            //then the partition is sure enough of itself
            trimmedListOfFinalPartitionBool[par] = true;
        }

    }

    vector<Partition> trimmedListOfFinalPartition;
    for (auto p = 0 ; p < listOfFinalPartitions.size() ; p++){
        if (trimmedListOfFinalPartitionBool[p]){
            trimmedListOfFinalPartition.push_back(listOfFinalPartitions[p]);
            // cout << "remaining partition : " << endl;
            // listOfFinalPartitions[p].print();
        }
    }

    return trimmedListOfFinalPartition;
}

//input : a list of all binary partitions found in the reads
//output : a vector a big as the number of reads, where numbers correspond to groups of reads
vector<int> threadHaplotypes2(vector<Partition> &listOfFinalPartitions, int numberOfReads){

    //as a first step, we'll compute a score for each partition depending on how confident it is    
    for (int par = 0 ; par < listOfFinalPartitions.size() ; par++){

        //compute a score evaluating the certainty of the partition
        listOfFinalPartitions[par].compute_conf();
    }

    struct {
        bool operator()(Partition a, Partition b) const { return a.get_conf() > b.get_conf(); }
    } customLess;
    std::sort(listOfFinalPartitions.begin(), listOfFinalPartitions.end(), customLess);

    cout << "Here are all the partitions, sorted : " << endl;
    for (auto p : listOfFinalPartitions){
        p.print();
    }

    //now we have a sorted list of final partitions in decreasing order
    vector<int> res (numberOfReads, -1); //vector containing all the clusters

    int n = 0;
    for (auto p : listOfFinalPartitions){
        extend_with_partition_if_compatible(res, p, n);
        n++;
    }

    return res;
}

//input : all the already threaded haplotypes and a new partition
//output : a bool telling if the new partition is compatible with already threaded haplotypes. If yes, alreadyThreadedHaplotype modified
bool extend_with_partition_if_compatible(vector<int> &alreadyThreadedHaplotypes, Partition &extension, int partitionIndex){

    //compatibility is defined as : either the 0s or the 1s of the extension all fall squarely within one already defined haplotype
    std::unordered_map <int, int> repartitionOf0s;
    std::unordered_map <int, int> repartitionOf1s;

    bool compatible = false;

    auto idxs = extension.getReads();
    auto content = extension.getPartition();

    int n = 0;
    int numberOf1s = 0;
    int numberOf0s = 0;
    vector<int> extension_vector (alreadyThreadedHaplotypes.size(), 0);
    for (auto idx : idxs){
        if (alreadyThreadedHaplotypes[idx] != 0)
        {
            if (content[n]==1){
                if (repartitionOf1s.find(alreadyThreadedHaplotypes[idx]) == repartitionOf1s.end()){
                    repartitionOf1s[alreadyThreadedHaplotypes[idx]] = 1;
                }
                else {
                    repartitionOf1s[alreadyThreadedHaplotypes[idx]] += 1;
                }
                numberOf1s++;
            }
            else if (content[n]==-1){
                if (repartitionOf0s.find(alreadyThreadedHaplotypes[idx]) == repartitionOf0s.end()){
                    repartitionOf0s[alreadyThreadedHaplotypes[idx]] = 1;
                }
                else {
                    repartitionOf0s[alreadyThreadedHaplotypes[idx]] += 1;
                }
                numberOf0s++;
            }
        }
        extension_vector[idx] = content[n];
        n++;
    }

    //find the best haplotype for 1s
    float max1 = 0;
    int maxClust1 = -1;
    for (auto pair : repartitionOf1s){
        if (pair.first != -1){
            if (pair.second > max1){
                maxClust1 = pair.first;
                max1 = pair.second;
            }
        }
    }

    //find the best haplotype for 0s
    float max0 = 0;
    int maxClust0 = -1;
    for (auto pair : repartitionOf0s){
        if (pair.first != -1){
            if (pair.second > max0){
                maxClust0 = pair.first;
                max0 = pair.second;
            }
        }
    }

    //first see if one haplotype fall squarely in a non-haplotyped zone
    if (max1 < 0.1*numberOf1s){
        compatible = true;
        for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
            if (extension_vector[r] == 1){
                alreadyThreadedHaplotypes[r] = partitionIndex*2+1;
            }
        }
    }
    if (max0 < 0.1*numberOf0s){
        compatible = true;
        for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
            if (extension_vector[r] == -1){
                alreadyThreadedHaplotypes[r] = partitionIndex*2;
            }
        }
    }

    //see if all the 1s fall squarely within one already threaded haplotype
    if (max1/numberOf1s > 0.9 && max1 >= 0.1*numberOf1s){ //yes !
        compatible = true;
        if (repartitionOf0s.find(maxClust1) == repartitionOf0s.end() || repartitionOf0s[maxClust1] < 0.1*max1){ //the 0s and the one donn't share maxClust
            for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
                if (alreadyThreadedHaplotypes[r] == maxClust1 && extension_vector[r] == -1){ //whuu, was it really well clustered ?
                    alreadyThreadedHaplotypes[r] = -1;
                }
                else if (alreadyThreadedHaplotypes[r] == -1 && extension_vector[r] == 1){ // let's extend maxClust
                    alreadyThreadedHaplotypes[r] = maxClust1;
                }
            }
        }
        else{ //then there are 0s on the same already threaded cluster as where the 1s are now : the already threaded cluster was too big
            for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
                if (alreadyThreadedHaplotypes[r] == maxClust1 && extension_vector[r] != 1){
                    alreadyThreadedHaplotypes[r] = -1;
                }
                else if (extension_vector[r] == 1){  
                    alreadyThreadedHaplotypes[r] = partitionIndex*2+1;
                }
            }
        }
    }

    //see if all the 0s fall squarely within one already threaded haplotype
    if (numberOf0s == 0 && max0 >= 0.1*numberOf0s){ //yes !
        compatible = true;
        if (repartitionOf1s.find(maxClust0) == repartitionOf1s.end() || repartitionOf1s[maxClust0] < 0.1*max0){ //the 0s and the one donn't share maxClust
            for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
                if (alreadyThreadedHaplotypes[r] == maxClust0 && extension_vector[r] == 1){ //whuu, was it really well clustered ?
                    alreadyThreadedHaplotypes[r] = -1;
                }
                else if (alreadyThreadedHaplotypes[r] == -1 && extension_vector[r] == -1){ // let's extend maxClust
                    alreadyThreadedHaplotypes[r] = maxClust0;
                }
            }
        }
        else{ //then there are 1s on the same already threaded cluster as where the 0s are now : the already threaded cluster was too big
            for (int r = 0 ; r < alreadyThreadedHaplotypes.size() ; r++){
                if (alreadyThreadedHaplotypes[r] == maxClust0 && extension_vector[r] != -1){
                    alreadyThreadedHaplotypes[r] = -1;
                }
                else if (extension_vector[r] == -1){  //whuu, was it really well clustered ?
                    alreadyThreadedHaplotypes[r] = partitionIndex*2;
                }
            }
        }
    }

    return compatible;

}

//input : the list of threaded clusters and snps
//output : reassign ALL reads to the cluster where they fit best
vector<int> rescue_reads(vector<int> &threadedClusters, vector<Column> &snps, vector<size_t> &suspectPostitions){

    cout << "Rescuing reads\r" << endl;

    //list all the clusters in a map
    std::unordered_map <int, int> clusterIdx;
    int idx = 0;
    for (auto clust : threadedClusters){
        if (clust != -1){
            if (clusterIdx.find(clust) == clusterIdx.end()) {
                clusterIdx[clust] = idx;
                idx++;
            }
        } 
    }

    if (clusterIdx.size() == 0){
        return vector<int> (threadedClusters.size(), 0);
    }

    robin_hood::unordered_flat_map<char, short> bases2content;
    bases2content['A'] = 0;
    bases2content['C'] = 1; 
    bases2content['G'] = 2;
    bases2content['T'] = 3;
    bases2content['-'] = 4;
    bases2content['?'] = 5;

    //create a vector counting for each read what cluster fits best
    vector<vector<int>> bestClusters (threadedClusters.size(), vector<int> (clusterIdx.size(), 0));

    //now iterate through the suspect positions
    for (auto position : suspectPostitions){

        //look at what base is normal for each cluster
        vector<vector<int>> basesForEachCluster(clusterIdx.size(), vector<int> (5, 0));
        int c = 0;
        for (auto read : snps[position].readIdxs){
            if (threadedClusters[read] != -1 && snps[position].content[c] != '?'){
                basesForEachCluster[clusterIdx[threadedClusters[read]]][bases2content[snps[position].content[c]]] += 1;
            }
            c++;
        }

        vector<char> clusterBase (clusterIdx.size(), 0);
        bool sure = true; //bool marking if all cluster agree within themselves
        for (auto c = 0 ; c < clusterBase.size() ; c++){
            char bestBase = '?';
            int bestBaseNb = 0;
            int totalBaseNb = 0;
            for (auto b = 0 ; b < 5 ; b++){
                int thisBaseNb = basesForEachCluster[c][b]; 
                totalBaseNb += thisBaseNb;
                if (thisBaseNb > bestBaseNb){
                    bestBaseNb = thisBaseNb;
                    bestBase = "ACGT-"[b];
                }
            }
            clusterBase[c] = bestBase;
            if (float(bestBaseNb)/totalBaseNb < 0.8){
                sure = false;
                break; //this position is not worth looking at
            }
        }

        //now each cluster has its base, let's update bestClusters
        if (sure){
            int c =0;
            for (auto read :snps[position].readIdxs){
                for (auto clust = 0 ; clust < clusterIdx.size() ; clust++){
                    if (snps[position].content[c] == clusterBase[clust]){
                        bestClusters[read][clust] += 1;
                    }
                    else{
                        bestClusters[read][clust] -= 1;
                    }
                }
                c++;
            }
        }
    }

    //now for each read we know at how many positions it agrees with each cluster : find best cluster
    vector<int> newClusters (threadedClusters.size(), -1);
    for (auto r = 0 ; r < newClusters.size() ; r++){

        // cout << "for read " << r << ", here is the bestCluster :"; for(auto i : bestClusters[r]){cout << i << ",";} cout << endl; 
        auto maxIterator = std::max_element(bestClusters[r].begin(), bestClusters[r].end());
        if (*maxIterator > 0){
            newClusters[r] = std::distance(bestClusters[r].begin() , maxIterator );
        }
        // else {
        //     cout << "wow, this read " << r << " has 0 positions, sad sad sad " << r << endl;
        // }
    }

    return newClusters;
}









