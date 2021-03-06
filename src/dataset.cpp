#include "dataset.h"
#include "frame.h"
#include "measure.h"
#include "mark.h"
#include "config.h"

namespace calibcamodo {

using namespace std;
using namespace cv;
using namespace aruco;

Dataset::Dataset() {

    mNumFrame = Config::NUM_FRAME;
    mMarkerSize = Config::MARK_SIZE;
    mstrFoldPathMain = Config::STR_FOLDERPATH_MAIN;
    mstrFoldPathImg = Config::STR_FOlDERPATH_IMG;
    mstrFilePathCam = Config::STR_FILEPATH_CAM;
    mstrFilePathOdo = Config::STR_FILEPATH_ODO;

 // load camera intrinsics
    mCamParam.readFromXMLFile(mstrFilePathCam);

    // set aruco mark detector
    int ThePyrDownLevel = 0;
    int ThresParam1 = 19;
    int ThresParam2 = 15;
    mMDetector.pyrDown(ThePyrDownLevel);
    mMDetector.setCornerRefinementMethod(MarkerDetector::LINES);
    mMDetector.setThresholdParams(ThresParam1, ThresParam2);

    // select keyframe
    mThreshOdoLin = Config::DATASET_THRESH_KF_ODOLIN;
    mThreshOdoRot = Config::DATASET_THRESH_KF_ODOROT;
}

Dataset::~Dataset(){}

void Dataset::CreateFrame() {

    // load image
    map<int, Mat> mapId2Img;
    for (int i = 0; i < mNumFrame; ++i) {
        string strImgPath = mstrFoldPathImg + to_string(i) + ".bmp";
        Mat img = imread(strImgPath);
        if (!img.empty())
            mapId2Img[i] = img;
    }

    // load odometry
    map<int, Se2> mapId2Odo;
    ifstream logFile_stream(mstrFilePathOdo);
    string str_tmp;
    while(getline(logFile_stream, str_tmp)) {
        // read time info
        Se2 odo_tmp;
        int id_tmp;
        if (ParseOdoData(str_tmp, odo_tmp, id_tmp)) {
            mapId2Odo[id_tmp] = odo_tmp;
        }
    }

    // build frame vector
    int maxIdImg = mapId2Img.crbegin()->first;
    int maxIdOdo = mapId2Odo.crbegin()->first;
    int maxId = maxIdImg > maxIdOdo ? maxIdImg : maxIdOdo;
    for (int i = 0; i <= maxId; ++i) {
        const auto iterImg = mapId2Img.find(i);
        const auto iterOdo = mapId2Odo.find(i);
        if (iterImg != mapId2Img.cend() && iterOdo != mapId2Odo.cend()) {
            PtrFrame pf = make_shared<Frame>(iterImg->second, iterOdo->second, i);
            InsertFrame(pf);
        }
    }

    return;
}

bool Dataset::ParseOdoData(const string _str, Se2 &_odo, int &_id) {
    vector<string> vec_str = SplitString(_str, " ");

    // fail
    if (vec_str[0] == "#") return false;

    // read data
    _id = atof(vec_str[0].c_str());
    _odo.x = atof(vec_str[3].c_str());
    _odo.y = atof(vec_str[4].c_str());
    _odo.theta = atof(vec_str[5].c_str());
    return true;
}

vector<string> Dataset::SplitString(const string _str, const string _separator) {
    string str = _str;
    vector<string> vecstr_return;
    int cut_at;
    while ((cut_at = str.find_first_of(_separator)) != str.npos) {
        if (cut_at > 0) {
            vecstr_return.push_back(str.substr(0, cut_at));
        }
        str = str.substr(cut_at + 1);
    }
    if (str.length() > 0) {
        vecstr_return.push_back(str);
    }
    return vecstr_return;
}

void Dataset::CreateKeyFrame() {

    PtrKeyFrame pKeyFrameLast = make_shared<KeyFrame>(**msetpFrame.cbegin(), mCamParam, mMDetector, mMarkerSize);
    InsertKf(pKeyFrameLast);

    for (auto ptr : msetpFrame) {
        PtrFrame pFrameNew = ptr;
        Se2 dodo = pFrameNew->GetOdo() - pKeyFrameLast->GetOdo();
        double dl = sqrt(dodo.x*dodo.x + dodo.y*dodo.y);
        double dr = abs(dodo.theta);
        Mat info = Mat::eye(3,3,CV_32FC1);
        if (dl > mThreshOdoLin || dr > mThreshOdoRot) {
            PtrKeyFrame pKeyFrameNew = make_shared<KeyFrame>(*pFrameNew, mCamParam, mMDetector, mMarkerSize);
            InsertKf(pKeyFrameNew);
            PtrMsrSe2Kf2Kf pMeasureOdo = make_shared<MeasureSe2Kf2Kf>(dodo, info, pKeyFrameLast, pKeyFrameNew);
            msetMsrOdo.insert(pMeasureOdo);
            pKeyFrameLast = pKeyFrameNew;
        }
    }
}

bool Dataset::InsertFrame(PtrFrame ptr) {
    int id = ptr->GetId();
    if(mmapId2pFrame.count(id)) {
        return false;
    }

    msetpFrame.insert(ptr);
    mmapId2pFrame[id] = ptr;
    return true;
}

bool Dataset::DeleteFrame(PtrFrame ptr) {
    auto iter = msetpFrame.find(ptr);
    if (iter == msetpFrame.cend()) {
        return false;
    }
    msetpFrame.erase(iter);
    mmapId2pFrame.erase(ptr->GetId());
    return true;
}

bool Dataset::InsertKf(PtrKeyFrame ptr) {
    int id = ptr->GetId();
    if(mmapId2pKf.count(id)) {
        return false;
    }
    msetpKf.insert(ptr);
    mmapId2pKf[id] = ptr;
    return true;
}

bool Dataset::DeleteKf(PtrKeyFrame ptr) {
    auto iter = msetpKf.find(ptr);
    if (iter == msetpKf.cend()) {
        return false;
    }
    msetpKf.erase(iter);
    mmapId2pKf.erase(ptr->GetId());
    return true;
}


bool Dataset::InsertMk(PtrArucoMark& ptr) {
    int id = ptr->GetId();
    if(mmapId2pMk.count(id)) {
        ptr = mmapId2pMk[id];
        return false;
    }
    msetpMk.insert(ptr);
    mmapId2pMk[id] = ptr;
    return true;
}

bool Dataset::DeleteMk(PtrArucoMark ptr) {
    auto iter = msetpMk.find(ptr);
    if (iter == msetpMk.cend()) {
        return false;
    }
    msetpMk.erase(iter);
    mmapId2pMk.erase(ptr->GetId());
    return true;
}

PtrArucoMark Dataset::FindMk(int id) {
    auto iter = mmapId2pMk.find(id);
    if (iter != mmapId2pMk.cend())
        return iter->second;
    else
        return nullptr;
}

void Dataset::CreateMarkMeasure() {

    for (auto pkf : msetpKf) {
        const vector<Marker>& vecMeasureAruco = pkf->GetMsrAruco();
        for (auto measure_aruco : vecMeasureAruco) {
            // read data from aruco detect
            int id = measure_aruco.id;
            Mat rvec = measure_aruco.Rvec;
            Mat tvec = measure_aruco.Tvec;
            Mat info = Mat::eye(6,6,CV_32FC1);

            // add new aruco mark into dataset
            PtrArucoMark pamk = make_shared<ArucoMark>(id);
            InsertMk(pamk);

            // add new measurement into dataset
            PtrMsrKf2AMk pmeas = make_shared<MeasureKf2AMk>(rvec, tvec, info, pkf, pamk);
            InsertMsrMk(pmeas);
        }
    }
}

bool Dataset::InsertMsrMk(PtrMsrKf2AMk pmsr) {
    msetMsrMk.insert(pmsr);
    PtrKeyFrame pKf = pmsr->pKf;
    PtrArucoMark pMk = pmsr->pMk;
    pKf->InsertMsrMk(pmsr);
    pMk->InsertMsrMk(pmsr);
}

void Dataset::InitKf(Se3 _se3bc) {
    for(auto ptr : msetpKf) {
        PtrKeyFrame pKf = ptr;

        Se2 se2odo = pKf->GetOdo();
        Se2 se2wb = se2odo;
        Se3 se3wb = Se3(se2wb);
        Se3 se3wc = se3wb+_se3bc;

        //        cerr << "se2odo:" << se2odo << endl;
        //        cerr << "se3wb:" << se3wb << endl;
        //        cerr << "se3wc:" << se3wc << endl;

        pKf->SetPoseBase(se2odo);
        pKf->SetPoseCamera(se3wc);

        //        Se3 se3wc_b = pKf->GetPoseCamera();
        //        cerr << "se3wc:" << se3wc_b << endl;
        //        cerr << endl;
    }
}

void Dataset::InitMk() {
    for(auto ptr : msetpMk) {
        PtrArucoMark pMk = ptr;
        set<PtrMsrKf2AMk> setpMsr = pMk->GetMsr();
        if(!setpMsr.empty()) {
            PtrKeyFrame pKf = (*setpMsr.cbegin())->pKf;
            Se3 se3wc = pKf->GetPoseCamera();
            Se3 se3cm = (*setpMsr.cbegin())->se3;
            Se3 se3wm = se3wc+se3cm;
            pMk->SetPose(se3wm);

            // DEBUG
            //            cerr << "se3wc" << se3wc.rvec.t() << se3wc.tvec.t() << endl;
            //            cerr << "se3cm" << se3cm.rvec.t() << se3cm.tvec.t() << endl;
            //            cerr << "se3wm" << se3wm.rvec.t() << se3wm.tvec.t() << endl;
            //            cerr << endl;
        }
    }
}

}
