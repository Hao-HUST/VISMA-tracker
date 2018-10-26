//
// Created by feixh on 10/25/17.
//
// Common utilities.
#pragma once
#include "eigen_alias.h"

// stl
#include <chrono>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <fstream>

// 3rdparty
#include "opencv2/core.hpp"
#include "json/json.h"
#include "glog/logging.h"


namespace feh {

/// \brief colorful texts for terminal
struct TermColor {
    static const std::string red;
    static const std::string green;
    static const std::string blue;
    static const std::string cyan;
    static const std::string yellow;
    static const std::string magenta;
    static const std::string gray;
    static const std::string white;
    static const std::string bold;
    static const std::string end;
    static const std::string endl;
private:
    TermColor() = delete;
};


/// \brief timer
class Timer {
public:

    enum Unit {
        MILLISEC,
        MICROSEC,
        NANOSEC
    };

public:
    friend std::ostream &operator<<(std::ostream &os, const Timer &obj);

    Timer() :
        module_name_("default"),
        report_average(true) {}

    Timer(std::string module_name_) :
        module_name_(module_name_),
        report_average(true) {}

    void Tick() {
        Tick("anonymous event");
    }

    void Tick(const std::string &event) {
        start_[event] = std::chrono::high_resolution_clock::now();
    }

    float Tock() {
        return Tock("anonymous event");
    }

    /// \param: Return timing in milliseconds. Also record timing in look up table.
    float Tock(const std::string &event) {
        float timing(Elapsed(event).count());
        if (look_up_table_.count(event)) {
            look_up_table_[event] += timing;
            counter_[event] += 1;
        } else {
            look_up_table_[event] = timing;
            counter_[event] = 1;
        }
        return timing * 1e-6;
    }

    void Reset() {
        start_.clear();
        look_up_table_.clear();
        counter_.clear();
    }

    float LookUp(const std::string &event, const Unit unit = MILLISEC, bool average = false) const {
        if (!look_up_table_.count(event)) return -1;
        switch (unit) {
            case MILLISEC:
                return look_up_table_.at(event) * 1e-6 / (average ? counter_.at(event) + 0.01 : 1.0);
                break;
            case MICROSEC:
                return look_up_table_.at(event) * 1e-3 / (average ? counter_.at(event) + 0.01 : 1.0);
                break;
            case NANOSEC:
                return look_up_table_.at(event) / (average ? counter_.at(event) + 0.01 : 1.0);
                break;
            default:
                return -1;
                break;
        }
    }

    std::chrono::nanoseconds Elapsed(std::string event) const {
        auto tmp = std::chrono::high_resolution_clock::now();
        assert(start_.count(event) && ("event[" + event + "] not found").c_str());
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            tmp - start_.at(event));
    }

private:
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> start_;
    std::map<std::string, float> look_up_table_;
    std::unordered_map<std::string, int> counter_;
    std::string module_name_;

public:
    bool report_average;
};

/// \brief generate a random matrix of dimension N x M
template<int N, int M = N>
Eigen::Matrix<ftype, N, M> RandomMatrix(
    ftype meanVal = 0.0,
    ftype stdVal = 1.0,
    std::shared_ptr<std::knuth_b> p_engine = nullptr) {

    using ftype = ftype;

    std::normal_distribution<ftype> dist(meanVal, stdVal);
    Eigen::Matrix<ftype, N, M> v;
    if (p_engine == nullptr) {
        std::default_random_engine engine;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < M; ++j) {
                v(i, j) = dist(engine);
            }
    } else {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < M; ++j) {
                v(i, j) = dist(*p_engine);
            }
    }
    return v;
};


/// \brief generate a random vecotr of dimension N
template<int N>
Eigen::Matrix<ftype, N, 1> RandomVector(
    ftype meanVal = 0.0,
    ftype stdVal = 1.0,
    std::shared_ptr<std::knuth_b> p_engine = nullptr) {

    using ftype = ftype;

    std::normal_distribution<ftype> dist(meanVal, stdVal);
    Eigen::Matrix<ftype, N, 1> v;
    if (p_engine == nullptr) {
        std::default_random_engine engine;
        for (int i = 0; i < N; ++i) {
            v(i) = dist(engine);
        }
    } else {
        for (int i = 0; i < N; ++i) {
            v(i) = dist(*p_engine);
        }

    }
    return v;
};

/// \brief detect if any compoennt of a matrix is nan
template<int N, int M = 1>
bool anynan(const Eigen::Matrix<ftype, N, M> &m) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < M; ++j)
            if (std::isnan(m(i, j)))
                return true;
    return false;
};

/// \brief print enum value as int
template <typename Enumeration>
typename std::underlying_type<Enumeration>::type as_integer(Enumeration const value) {
    using type = typename std::underlying_type<Enumeration>::type;
    return static_cast<type>(value);
}


/// \brief: Convert from std vector of Eigen vectors to Eigen matrix.
template <typename T, int DIM=3>
Eigen::Matrix<T, Eigen::Dynamic, DIM>
StdVectorOfEigenVectorToEigenMatrix(const std::vector<Eigen::Matrix<T, DIM, 1> > &v) {
    Eigen::Matrix<T, Eigen::Dynamic, DIM> out;
    out.resize(v.size(), DIM);
    for (int i = 0; i < v.size(); ++i) {
        out.row(i) = v[i];
    }
    return out;
}


/// \brief: Convert from an Eigen matrix type to a std vector of Eigen vectors.
template <typename T, int DIM=3>
std::vector<Eigen::Matrix<T, DIM, 1>>
EigenMatrixToStdVectorOfEigenVector(const Eigen::Matrix<T, Eigen::Dynamic, DIM> &m) {
    std::vector<Eigen::Matrix<T, DIM, 1>> out(m.rows());
    for (int i = 0; i < m.rows(); ++i) out[i] = m.row(i);
    return out;
};

/// \brief: Convert from a std vector of Eigen vectors to an Eigen matrix.
template <typename T, int DIM=3>
Eigen::Matrix<T, DIM, 1> StdVectorOfEigenVectorMean(const std::vector<Eigen::Matrix<T, DIM, 1>> &v) {
    return StdVectorOfEigenVectorToEigenMatrix(v).colwise().mean();
};

/// \brief: Compute the rotation matrix to align two vectors.
template <typename T>
Eigen::Matrix<T, 3, 3> RotationBetweenVectors(Eigen::Matrix<T, 3, 1> u, Eigen::Matrix<T, 3, 1> v) {
    return Eigen::Quaternion<T>::FromTwoVectors(u, v).toRotationMatrix();
};



inline constexpr int cube(int x) { return x * x * x; }
inline constexpr int square(int x) { return x * x; }

template <int L=8>
std::vector<std::array<uint8_t, 3>> GenerateRandomColorMap() {
    uint8_t q = 255 / L;
    std::vector<std::array<uint8_t,3>> cm(cube(8));
    auto generator = std::knuth_b(0);
    for (int i = 0; i < cube(L); ++i) {
        cm[i] = {q * (i%8u), q * ((i/8)%8u), q * ((i/64)%8u)};
        std::shuffle(cm[i].begin(), cm[i].end(), generator);
    }
    std::shuffle(cm.begin()+1, cm.end(), generator);
    return cm;
};

/// \brief: List files with the given extension.
/// \param path: Directory of files.
/// \param extension: File extension.
/// \param filenames: List of returned file names.
/// \return: True if reads the file list successfully.
bool Glob(const std::string &path, const std::string &extension, std::vector<std::string> &filenames);

bool Glob(const std::string &path,
          const std::string &extension,
          const std::string &prefix,
          std::vector<std::string> &filenames);

template <typename T>
T BilinearSample(const cv::Mat &img, const Eigen::Matrix<float, 2, 1> &xy) {
    int col{xy(0)};
    int row{xy(1)};
    T v1 = (row+1-xy(1)) * (col+1-xy(0)) * img.at<T>(row, col);
    T v2 = (xy(1)-row) * (col+1-xy(0)) * img.at<T>(row+1, col);
    T v3 = (xy(1)-row) * (xy(0)-col) * img.at<T>(row+1, col+1);
    T v4 = (row+1-xy(1)) * (xy(0)-col) * img.at<T>(row, col+1);
    return v1 + v2 + v3 + v4;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// I/O UTILITIES
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class MeshIO : public std::exception {
    virtual const char* what() const throw() {
        return "Mesh IO Exception";
    }
};

/// \brief: Load vertices and faces from an .obj file.
/// \param obj_file: The .obj file.
/// \param vertices: Vertices of the mesh.
/// \param faces: Faces of the mesh.
std::tuple<MatXf, MatXi> LoadMesh(const std::string &file);
bool LoadMesh(const std::string &file, MatXf &V, MatXi &F);

// bool LoadMeshFromObjFile(const std::string &obj_file, std::vector<float> &vertices, std::vector<int> &faces);
// std::tuple<std::vector<float>, std::vector<int>> LoadMeshFromObjFile(const std::string &obj_file);
// bool LoadMeshFromPlyFile(const std::string &obj_file, std::vector<float> &vertices, std::vector<int> &faces);
// std::tuple<std::vector<float>, std::vector<int>> LoadMeshFromPlyFile(const std::string &obj_file);

/// \brief: Load edgemap from protobuf file.
bool LoadEdgeMap(const std::string &filename, cv::Mat &edge);
/// \brief: Load a list of mesh file paths.
/// \param root: Root directory of the CAD database. All the meshes are put directly under this directory.
/// \param cat_json: Json file of the list of meshes of a certain category.
std::vector<std::string> LoadMeshDatabase(const std::string &root, const std::string &cat_json);

/// \brief: Convert protobuf repeated field to Eigen matrix.
Mat4f SE3FromArray(float *data);
Mat4f SE3FromArray(double *data);

enum class JsonMatLayout {
    OneDim,
    RowMajor,
    ColMajor
};
/// \brief load NxM double matrix from json file.
/// \param v: json record
/// \param key: the key
/// \param layout: Whether the matrix is arranged as an one-dim array or two-dim matrix.
template<typename T=ftype, int N=3, int M = N>
Eigen::Matrix<T, N, M> GetMatrixFromDynamic(
    const Json::Value &v,
    const std::string &key,
    JsonMatLayout layout=JsonMatLayout::OneDim) {

    Eigen::Matrix<T, N, M> ret;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < M; ++j)
            if (layout == JsonMatLayout::OneDim) {
                ret(i, j) = v[key][i * M + j].asDouble();
            } else if (layout == JsonMatLayout::RowMajor) {
                ret(i, j) = v[key][i][j].asDouble();
            } else {
                ret(i, j) = v[key][j][i].asDouble();
            }
    return ret;
}

/// \brief load N-dim double vector from json file
template<typename T=ftype, int N>
Eigen::Matrix<T, N, 1> GetVectorFromDynamic(
    const Json::Value &v,
    const std::string &key) {

    return GetMatrixFromDynamic<T, N, 1>(v, key);
};

template<typename Derived>
void WriteMatrixToDynamic(Json::Value &d, const std::string &key, const Eigen::MatrixBase<Derived> &m) {
    // d[key] = Json::Value;
    for (int i = 0; i < m.rows(); ++i)
        for (int j = 0; j < m.cols(); ++j)
            d[key].append(m(i, j));
}

template<typename Derived>
void WriteMatrixToJsonObj(Json::Value &d, const std::string &key, const Eigen::MatrixBase<Derived> &m) {
    // d[key] = Json::Value;
    for (int i = 0; i < m.rows(); ++i)
        for (int j = 0; j < m.cols(); ++j)
            d[key].append(m(i, j));
}

/// \brief: Save opencv mat to file in txt or binary form (if the flag binary is turned on).
template <typename T>
void SaveMatToFile(const std::string &filename, const cv::Mat &mat, bool binary=false) {
    CHECK(!binary) << "binary mode not implemented";
    std::ofstream ofs(filename, std::ios::out);
    CHECK(ofs.is_open()) << "failed to open output file @ " << filename;
    for (int i = 0; i < mat.rows; ++i) {
        for (int j = 0; j < mat.cols; ++j) {
            ofs << (float) mat.at<T>(i, j) << " ";
        }
        ofs << "\n";
    }
    ofs.close();
}

/// \brief: Merge two dynamic objects and return the merged one.
Json::Value MergeDynamic(const Json::Value &a, const Json::Value &b);
/// \brief: Merge two dynamic objects and return the merged one.
Json::Value MergeJsonObj(const Json::Value &a, const Json::Value &b);

}
