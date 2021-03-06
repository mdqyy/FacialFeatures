#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <opencv2/video/tracking.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/gpu/gpu.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include "detect_util.hpp"
#include "svm.h"

#define PI 3.1415926536

using namespace std;
using namespace cv;
namespace fs = boost::filesystem;

void retrieve_hog(const string & line, vector<float> & values) {
  ifstream fi(line.c_str(), ifstream::in);
  string feats;
  while (getline(fi, feats)) {
    stringstream iss(feats);
    string v;
    while (getline(iss, v, ' ')) {
      values.push_back(atof(v.c_str()));
    }
  }
}

void loadPointsFile(string path, vector<pair<Point2f, Point2f> > & canonicalPoints) {
  canonicalPoints.clear();
  ifstream fi(path.c_str(), ifstream::in);
  string line, x, y;
  vector<Point2f> tmp;
  for (int i = 0; i < 10; i++) {
    getline(fi, line);
    stringstream linestream(line);
    getline(linestream, x, ' ');
    getline(linestream, y);
    tmp.push_back(Point2f(atof(x.c_str()), atof(y.c_str())));
  }
  Point2f gl((tmp[0].x + tmp[1].x) / 2.0,
            (tmp[0].y + tmp[1].y) / 2.0);
  Point2f gr((tmp[6].x + tmp[7].x) / 2.0,
            (tmp[6].y + tmp[7].y) / 2.0);
  canonicalPoints.push_back(make_pair(gl, gr));
  canonicalPoints.push_back(make_pair(tmp[2], tmp[3]));
}

void align(const Mat & src, Mat & dst, pair<Point2f, Point2f> & points) {
  if (src.data == NULL) {
    cerr << "The image is invalid to be aligned" << endl;
    return;
  }

  Point2f l = points.first;
  Point2f r = points.second;

  double theta = atan2(r.y - l.y, r.x - l.x);
  theta = theta / PI * 180;
  Mat rot_mat = getRotationMatrix2D(l, theta, 1.0);
  warpAffine(src, dst, rot_mat, src.size());
  double dist = sqrt((l.x - r.x) * (l.x - r.x) + (l.y - r.y) * (l.y - r.y));
  double delta = 40 / dist;
  resize(dst, dst, Size(dst.cols * delta, dst.rows * delta));
  dst = dst(Rect(l.x * delta - 20, l.y * delta - 20, 80, 40));
}

void compute_hog(const vector<string> & urls, vector<vector<float> > & features, vector<float> & labels, float label) {
  HOGDescriptor hog(Size(80, 40), Size(16, 16), Size(8, 8), Size(2, 2),
		    9, -1, 0.2, true, 64);

  vector<Point> locations;
  for (unsigned int i = 0; i < urls.size(); i++) {
    vector<float> featureVector;
    Mat img;
    img = imread(urls[i].c_str(), 1);


    // I am not sure I have to convert the image to
    // gray scale to compute the hog feauture
    if (img.channels() == 3)
      cvtColor(img, img, CV_BGR2GRAY);

    if (img.data == NULL)
      continue;

    assert(img.cols == hog.winSize.width);
    assert(img.rows == hog.winSize.height);
    hog.compute(img, featureVector, Size(4, 4), Size(0, 0), locations);
    if (!featureVector.empty()) {
      features.push_back(featureVector);
      labels.push_back(label);
    }
  }
}

void get_urls(const string & path, vector<string> & urls) {
  urls.clear();
  ifstream fi(path.c_str(), ifstream::in);
  string line;
  while (getline(fi, line))
    urls.push_back(line);
  fi.close();
}

void standardize(vector<vector<float> > & features, vector<float> & mean, vector<float> & stddev) {
  unsigned int i, j;
  unsigned int num_features = features[0].size();
  cout << "size of features:  "  << num_features << endl;

  vector<float> tot;
  tot.reserve(num_features);
  mean.reserve(num_features);
  stddev.reserve(num_features);
  for (i = 0; i < num_features; i++) {
    tot.push_back(0);
    mean.push_back(0);
    stddev.push_back(0);
  }

  for (i = 0; i < num_features; i++)
    for (j = 0; j < features.size(); j++)
      tot[i] += features[j][i];

  for (i = 0; i < num_features; i++)
    mean[i] = tot[i] / features.size();

  cout << "mean computed" << endl;

  for (i = 0; i < num_features; i++) {
    float err = 0;
    for (j = 0; j < features.size(); j++)
      err += (mean[i] - features[j][i]) * (mean[i] - features[j][i]);
    err /= features.size();
    stddev[i] = sqrt(err);
  }

  cout << "stddev computed" << endl;

  for (i = 0; i < num_features; i++)
    for (j = 0; j < features.size(); j++)
      if (stddev[i] != 0)
	features[j][i] = (features[j][i] - mean[i]) / (2 * stddev[i]);
}

void train(vector<vector<float> >& features, vector<float>& labels, string save_dir) {
  fs::path path(fs::current_path());
  path /= save_dir;
  if (!fs::exists(path)) {
    fs::create_directory(path);
  }
  string model_file = (path / "model.t").string();
  string ms_file = (path / "ms_file.txt").string();
  vector<float> mean, stddev;

  standardize(features, mean, stddev);

  cout << "finish standardize" << endl;
  
  svm_parameter param;
  param.svm_type = C_SVC;
  param.kernel_type = LINEAR;
  param.degree = 3;
  param.gamma = 0.5;
  param.coef0 = 0;
  param.nu = 0.5;
  param.cache_size = 100;
  param.C = 1;
  param.eps = 1e-3;
  param.p = 0.1;
  param.shrinking = 1;
  param.probability = 0;
  param.nr_weight = 0;
  param.weight_label = NULL;
  param.weight = NULL;

  cout << "finish constructing svm_parameter" << endl;

  svm_problem prob;
  prob.l = (int)features.size();
  prob.y = new double[prob.l];
  unsigned int line_size = features[0].size();
  svm_node *x_space = new svm_node[(line_size + 1) * prob.l];
  prob.x = new svm_node *[prob.l];
  for (unsigned int i = 0; i < features.size(); i++) {
    for (unsigned int j = 0; j < line_size; j++) {
      x_space[(line_size + 1) * i + j].index = j + 1;
      x_space[(line_size + 1) * i + j].value = features[i][j];
    }
    x_space[(line_size + 1) * i + line_size].index = -1;
    prob.x[i] = &x_space[(line_size + 1) * i];
    prob.y[i] = labels[i];
  }

  cout << "svm_parameter and svm_problem are computed" << endl;

  svm_model * model = svm_train(&prob, &param);
  cout << "nr_class " << model->nr_class << endl;
  cout << "total #SV " << model->l << endl;

  svm_save_model(model_file.c_str(), model);

  cout << "saved model" << endl;

  cout << ms_file << endl;
  
  fstream file;
  file.open(ms_file.c_str(), ios::out);
  file << mean[0];
  for (unsigned int i = 1; i < mean.size(); i++)
  file << mean[i] << " ";
  file << endl;
  
  file << stddev[0];
  for (unsigned int i = 1; i < stddev.size(); i++)
  file << stddev[i] << " ";
  file << endl;
  file.close();
  
  cout << "Finished Train" << endl;
}

void retrieve_data(string p_file, string n_file, vector<vector<float> > & features, vector<float> & labels) {
  features.clear();
  labels.clear();
  vector<string> images;

  get_urls(p_file, images);
  compute_hog(images, features, labels, 1);

  get_urls(n_file, images);
  compute_hog(images, features, labels, -1);
}

double predict_hog(string hog_path, string model_dir) {
  fs::path path(fs::current_path());
  path /= model_dir;
  string model_file = (path / "model.t").string();
  string ms_file = (path / "ms_file.txt").string();
  vector<float> feature, mean, stddev;
  string line;
  ifstream fi(hog_path.c_str(), ifstream::in);
  getline(fi, line);
  retrieve_hog(line, feature);

  
  string v;
  ifstream fms(ms_file.c_str(), ifstream::in);
  getline(fms, line);
  stringstream ism(line);
  while (getline(ism, v, ' '))
    mean.push_back(atof(v.c_str()));
  
  getline(fms, line);
  stringstream iss(line);
  while (getline(iss, v, ' '))
    stddev.push_back(atof(v.c_str()));

  svm_model * model = svm_load_model(model_file.c_str());
  for (unsigned int i = 0; i < feature.size(); i++) {
    if (stddev[i] != 0)
      feature[i] = (feature[i] - mean[i]) / (2 * stddev[i]);
  }
  svm_node * tmp = new svm_node[feature.size() + 1];
  for (unsigned int i = 0; i < feature.size(); i++) {
    tmp[i].index = (int)i + 1;
    tmp[i].value = (double)(feature[i]);
  }
  tmp[feature.size()].index = -1;
  return svm_predict(model, tmp);
}
