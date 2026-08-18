
#pragma once

#include <fstream>
#include <string>
#include"TGraphErrors.h"
#include "TF1.h"
#include "TFile.h"
#include "TStyle.h"
#include "TFitResult.h"
#include "TFitResultPtr.h"

using namespace std;

namespace ndlar_light {
    class VBRCalibrator
    {
        public:
        struct RunElement {
            int run_number;
            float voltage;
            int VGAgain;
            bool active;
        };
        std::vector<RunElement> fRunTable;
        std::vector<GainCalibrator> fGainCalibrators;
        string filename;
        string filepath;
        bool GainFitsAvailable=false;
        bool VBRFitsAvailable=false;
        string kVBRFitsFile="VBRFits.root";

        ChannelMap fChannelMap; //to handle the selection of channels to run the analysis.

        VBRCalibrator(std::string f) : filename(f) {
            std::cout << "VBRCalibrator constructor called\n";
            LoadRunTable(filename);
            fChannelMap.LoadFromCSV(kDefaultChannelMapPath);
            cout << "ey eye yeye k this " << endl;
            fChannelMap.Print();
            SetGainCalibrators();
            /* Full process:
             1. ProcessSPEHist()
             2. PerformGainFits()
             3. FitGainVsVoltage()
             4. DumpVBR()
            */
            kVBRFitsFile = Form("%s_vbrFits.root", filename.c_str());
        }
        void Process(std::string outfile="outfile.root")
        {
//            ProcessSPEHist(); //loop over all runs.
//            PerformGainFits(); //loop over all runs.
            FitGainVsVoltage(); //loop over all runs.
//            DumpVBR(outfile);
        }
        void SetGainCalibrators(){
            for(auto r : fRunTable) {
                std::cout << "VBRCalibrator: Running GainCalibrator for run " << r.run_number << "\n";
                Run *run = new Run(filepath, r.run_number);
                run->SetChannelMap(fChannelMap); // Use the same channel map for all runs
                GainCalibrator gain_calibrator(run, r.voltage);
                fGainCalibrators.push_back(gain_calibrator);
            }
        }
        void ProcessSPEHist() {
            for (auto& gain_calibrator : fGainCalibrators) {
                gain_calibrator.fRun->SetChannelMap(fChannelMap);
                //reset channel map in case it a selection was applied.
                gain_calibrator.ProcessSPEHist();
            }
        }
        void PerformGainFits() {
            for (auto& gain_calibrator : fGainCalibrators) {
                gain_calibrator.fRun->SetChannelMap(fChannelMap);
                gain_calibrator.PerformGainFits();
            }
            GainFitsAvailable=true;
        }
        void ResetChannels()
        {
            std::cout << "VBRCalibrator: Deactivate all channels.\n";
            fChannelMap.ResetAll(false); // Deactivate all channels first
        }
        void SelectChannel(int adc, int channel) {
            std::cout << "VBRCalibrator: Selecting ADC " << adc << ", channel " << channel << "\n";
            fChannelMap.SetActive(adc, channel, true);
        }
        std::vector<std::pair<int, int>> GetSelectedChannels() {
            fChannelMap.UpdateSelectedChannels();
            return fChannelMap.GetSelectedChannels();
        }

        void LoadRunTable(std::string f)
        {
            // Load the calibration table from a CSV file
            // Implement the logic to read the CSV and populate the RunTable
            ifstream file(f);
            if (!file.is_open()) {
                cerr << "Error opening file: " << filename << std::endl;
                return;
            }
            std::string line;
            std::getline(file,filepath); //filepath in first line
            while (std::getline(file, line)) {
                if (line.empty()) continue;
                std::istringstream ss(line);
                std::string tok;
                RunElement ch;
                std::getline(ss, tok, ','); ch.run_number  = std::stoi(tok);
                std::getline(ss, tok, ','); ch.voltage     = std::stof(tok);
                std::getline(ss, tok, ','); ch.VGAgain     = std::stoi(tok);
                std::getline(ss, tok, ','); ch.active     = std::stoi(tok);
                if(ch.active) fRunTable.push_back(ch);
            }
            std::cout << "VBRCalibrator: " << fRunTable.size() << " calibration runs from " << filename << std::endl;
        }
        /*VBR results are per channel, since a set of samples at different voltages is needed 
        to calibrate a single channel. Each VBR use as an input a set of gain histograms, then results will include
        a vector of GainFit, one for each point in the gain vs voltage curve*/
        class Results{
            public:
            int adc;
            int channel;
            std::vector<GainCalibrator::GainFit> GainsVector;
            double vbr;
            double vbr_error;
            TGraphErrors *tgVBR=NULL;
            TF1 *fVBR=NULL;
            TFitResult *FitRes;
            Results(){}
            Results(int a, int c, std::vector<GainCalibrator::GainFit> gv) : adc(a), channel(c),
            GainsVector(gv) {}
            void Fit()
            {
                int i=0;
                tgVBR = new TGraphErrors();
                for (auto &gainfit : GainsVector) {
                    if (!gainfit.fitSuccesfull) {
                        std::cout << "Gain fit not successful for ADC " << adc << ", channel " << channel << ", voltage " << gainfit.fVoltage << "V.\n";
                        continue;
                    }
                    tgVBR->SetPoint(i, gainfit.fVoltage, gainfit.fGain);
                    tgVBR->SetPointError(i, 0, gainfit.fGainError);
                    i++;
                }
                tgVBR->SetMarkerStyle(20);
                tgVBR->SetLineWidth(2);
                tgVBR->SetMarkerSize(1.2);
                tgVBR->SetTitle("Gain vs Voltage; Voltage (V); Gain (ADC counts x ticks)");
                fVBR = new TF1("fVBR","pol1",50,58);
                fVBR->SetParNames("Inter.","Slope");
                TFitResultPtr r =tgVBR->Fit(fVBR,"RSE");
                FitRes = dynamic_cast<TFitResult*>(r->Clone());
                FillVBRFromFit();
            }
            void FillVBRFromFit()
            {
                // G = p0 + p1 * V -> VBR = -p0/p1;
                const double p0 = fVBR->GetParameter(0);
                const double p1 = fVBR->GetParameter(1);
                vbr = -p0 / p1;
                const double dVdp0 = -1.0 / p1;
                const double dVdp1 =  p0 / (p1 * p1);
                vbr_error = std::sqrt(
                    dVdp0 * dVdp0 * FitRes->CovMatrix(0, 0) +
                    dVdp1 * dVdp1 * FitRes->CovMatrix(1, 1) +
                    2.0 * dVdp0 * dVdp1 * FitRes->CovMatrix(0, 1)
                );
//                cout << p0 << " " << p1 << endl;
//                cout << vbr << " " << vbr_error << endl;
//                cout << FitRes->CovMatrix(0, 0) << " " << FitRes->CovMatrix(1, 1) << " " << FitRes->CovMatrix(0, 1) << endl;
            }


            // this GainsVector will be recovered from the GainCalibrator.
        };
        void LoadGainFitsFromFiles(){
            std::cout << "VBRCalibrator: LoadGainFitsFromFiles start\n";
            if(fGainCalibrators.size()==0){
                std::cout << "VBRCalibrator: LoadGainFitsFromFiles: fGainCalibrators already loaded?.\n";
                SetGainCalibrators();
            }
            for (auto &g : fGainCalibrators)
            {
                g.fRun->SetChannelMap(fChannelMap); // Ensure we use the same channel map
                g.LoadGainFits();
            }
            GainFitsAvailable=true;
            std::cout << "VBRCalibrator: LoadGainFitsFromFiles ended. Number of GainCalibrators loaded:  "<< fGainCalibrators.size() << " \n";
        }
        std::vector<Results> fVBRResults;
        void FitGainVsVoltage(){
            std::cout << "VBRCalibrator::FitGainVsVoltage start\n";
            fVBRResults.clear();
            if (!GainFitsAvailable) LoadGainFitsFromFiles();
            std::cout << "VBRCalibrator::FitGainVsVoltage Number of GainCalibrators : " << fGainCalibrators.size() << "\n";
            fGainCalibrators[0].Print();
            for ( auto &channel : GetSelectedChannels()) {
                int adc = channel.first;
                int ch = channel.second;
                cout << "VBRCalibrator::FitGainVsVoltage: Processing ADC " << adc << ", Channel " << ch << "\n";
                std::vector<GainCalibrator::GainFit> gains;
                std::vector<float> voltages;
                for (auto& gain_calibrator : fGainCalibrators) { //per run
//                    gain_calibrator.fRun->SetChannelMap(fChannelMap);
                    GainCalibrator::GainFit &fit = gain_calibrator.GetGainFitPerChannel(adc, ch);

                    gains.push_back(fit);
                    voltages.push_back(gain_calibrator.fVoltage);
                }
                Results myres(adc, ch, gains);
                fVBRResults.push_back(myres); // VBR and error will be filled after fitting
                // Now fit the gains vs voltages to get the VBR for this channel
                // Implement the fitting logic here and store the results
            }
            std::cout << "VBRCalibrator::FitGainVsVoltage Number of VBRResults : " << fVBRResults.size() << "\n";
            for (auto &result : fVBRResults) {
                result.Fit();
            }
            VBRFitsAvailable=true;
            std::cout << "VBRCalibrator::FitGainVsVoltage completed\n";
            DumpVBR("VBR_results.csv");
            DumpVBRFits();
        }
        void DumpVBRFits(){
            std::cout << "VBRCalibrator::DumpFits: Dumping VBR fits to " << kVBRFitsFile << "\n";
            TFile *file = new TFile(kVBRFitsFile.c_str(),"RECREATE");
            if(!file->IsOpen()) {
                throw std::runtime_error( "VBRCalibrator::DumpFits: Error opening file "+kVBRFitsFile+" for writing.\n");
            }
            for (auto &result : fVBRResults) {
                if (result.fVBR) {
                    cout << 1 << endl;
                    result.fVBR->Write(Form("fVBR_ADC%d_CH%d", result.adc, result.channel));
                }
                if (result.tgVBR) {
                    cout << 2 << endl;
                    result.tgVBR->Write(Form("tgVBR_ADC%d_CH%d", result.adc, result.channel));
                }
                if (result.FitRes) {
                    cout << 3 << endl;
                    result.FitRes->Write(Form("fitres_ADC%d_CH%d", result.adc, result.channel));
                }
                    cout << 4 << endl;
            }
            file->Close();
            std::cout << "VBRCalibrator::DumpFits: Completed dumping VBR fits\n";
        }
        void LoadVBRFits()
        {
            std::cout << "VBRCalibrator::LoadVBRFits: Loading VBR fits from " << kVBRFitsFile << "\n";
            TFile *file = new TFile(kVBRFitsFile.c_str(),"READ");
            fVBRResults.clear();
            if (!GainFitsAvailable) LoadGainFitsFromFiles();
            for ( auto &channel : GetSelectedChannels()) {
                int adc = channel.first;
                int ch = channel.second;
                std::vector<GainCalibrator::GainFit> gains;
                std::vector<float> voltages;
                for (auto& gain_calibrator : fGainCalibrators) { //per run
                    gain_calibrator.fRun->SetChannelMap(fChannelMap);
                    GainCalibrator::GainFit &fit = gain_calibrator.GetGainFitPerChannel(adc, ch);
                    gains.push_back(fit);
                    voltages.push_back(gain_calibrator.fVoltage);
                }
                Results result(adc, ch, gains);
                TF1* fFromFile= dynamic_cast<TF1*>(file->Get(Form("fVBR_ADC%d_CH%d", result.adc, result.channel)));
                TGraphErrors* tgFromFile = dynamic_cast<TGraphErrors*>(file->Get(Form("tgVBR_ADC%d_CH%d", result.adc, result.channel)));
                TFitResult* fitresFromFile = dynamic_cast<TFitResult*>(file->Get(Form("fitres_ADC%d_CH%d", result.adc, result.channel)));
                if (fitresFromFile != nullptr) {
                    result.FitRes = dynamic_cast<TFitResult*>(fitresFromFile->Clone());
                }
                if(fFromFile!= nullptr) {

                    result.fVBR = dynamic_cast<TF1*>(fFromFile->Clone());
                }
                if(tgFromFile!= nullptr) {
                    result.tgVBR = dynamic_cast<TGraphErrors*>(tgFromFile->Clone());
                }

                if (result.fVBR && result.tgVBR && result.FitRes) {
                    result.FillVBRFromFit();
                    cout << "******VBRCalibrator::LoadVBRFits: Loaded VBR for ADC " << result.adc << ", Channel " << result.channel
                         << ": VBR = " << result.vbr << " ± " << result.vbr_error << "\n";
                }
                else{
                    cout << result.fVBR << " " << result.tgVBR << " " << result.FitRes << "\n";
                    throw std::runtime_error(Form("VBRCalibrator::LoadVBRFits: Error loading VBR fit for ADC %d, Channel %d", result.adc, result.channel));
                }
                fVBRResults.push_back(result);
            }
            VBRFitsAvailable=true;
            file->Close();
            std::cout << "VBRCalibrator::LoadVBRFits: Completed loading VBR fits: Size:" << fVBRResults.size() << "\n";
        }

        void DumpVBR(string outfile){
            std::cout << "VBRCalibrator::DumpVBR: Dumping VBR results to " << outfile << "\n";
            ofstream file(outfile);
            if (!file.is_open()) {
                cerr << "Error opening file for writing: " << outfile << endl;
                return;
            }
            file << "ADC,Channel,VBR,VBR_Error\n";
            for (auto &result : fVBRResults) {
                file << result.adc << "," << result.channel << "," << result.vbr << "," << result.vbr_error << "\n";
            }
            file.close();

        }
        void PrintReport(){
            if(!GainFitsAvailable) LoadGainFitsFromFiles();
            if(!VBRFitsAvailable) LoadVBRFits();
            cout << VBRFitsAvailable << " " << GainFitsAvailable << "\n";
            if(!VBRFitsAvailable || !GainFitsAvailable) {
                std::cerr << "VBRCalibrator::PrintReport: Error, VBR or Gain fits not available.\n";
                return;
            }
            if(fVBRResults.size()==0) {
                std::cerr << "VBRCalibrator::PrintReport: Error, no VBR results available.\n";
                return;
            }
            std::cout << "VBRCalibrator::PrintReport: VBR results, pages: " << fVBRResults.size() << " \n";

            int j=0;
            for (auto &result : fVBRResults) { //one result per channel
                TCanvas *c = new TCanvas("Gain","Gain",800,600);
                c->DivideSquare(result.GainsVector.size()+1); //one entry per gain fit
                int i = 0;
                for (auto &gainfit : result.GainsVector)
                {
                    c->cd(i+1);
                    auto h = gainfit.h;
                    h->SetLineColor(ndlar_light::MyColors[0]);
                    h->SetLineWidth(2);
                    ndlar_light::HistName hn = ndlar_light::HistName::Parse(h->GetName());
                    h->SetTitle(Form("Run %d - adc %i - ch %i - %.1fV",hn.Run(), hn.ADC(), hn.Channel(), gainfit.fVoltage ));
                    gainfit.Draw(c->cd(i+1));
                    i++;
                }
                c->cd(result.GainsVector.size()+1);
                result.tgVBR->SetMarkerStyle(7);
                result.tgVBR->SetMarkerColor(ndlar_light::MyColors[1]);
                result.tgVBR->SetTitle(Form("Gain vs Voltage for ADC %d, Channel %d", result.adc, result.channel));

                gStyle->SetOptStat(0);    // no mostrar estadísticas generales
                gStyle->SetOptFit(111);   // chi2/ndf + valores de parámetros + errores    tgVBR->Draw("APE02");
                result.tgVBR->SetMarkerStyle(7);
                result.tgVBR->Draw("APE02");
                c->Modified();c->Update();

                auto* fitBox = dynamic_cast<TPaveStats*>(gPad->GetPrimitive("stats"));
                if (fitBox) {
                    // Posición relativa al pad: lado izquierdo.
                    fitBox->SetX1NDC(0.15);
                    fitBox->SetX2NDC(0.52);
                    fitBox->SetY1NDC(0.57);
                    fitBox->SetY2NDC(0.90);

                    // Letra más grande.
                    fitBox->SetTextSize(0.05);

                    fitBox->SetTextAlign(12);
                    fitBox->SetFillColor(0);
                    fitBox->SetBorderSize(1);
                }
                // A persistent annotation, visually attached below the ROOT fit box.
                auto* vbrBox = new TPaveText(0.15, 0.49, 0.52, 0.57, "NDC");
                vbrBox->SetFillColor(0);
                vbrBox->SetFillStyle(1001);
                vbrBox->SetBorderSize(1);
                vbrBox->SetTextAlign(12);
                vbrBox->SetTextFont(42);
                vbrBox->SetTextSize(0.05);

                vbrBox->AddText(
                    Form("V_{BR} = %.3f #pm %.3f V", result.vbr, result.vbr_error)
                );
                vbrBox->Draw();
                c->Modified();c->Update();
                cout << "VBRCalibrator::PrintReport: Printing page " << j+1 << " of " << fVBRResults.size() << "\n";
//                PauseExecution();
                if(fVBRResults.size()==1) c->Print("Report.pdf", "pdf");
                else if(j==0) c->Print("Report.pdf(", "pdf");
                else if (j==fVBRResults.size()-1) c->Print("Report.pdf)", "pdf");
                else c->Print("Report.pdf", "pdf");
                j++;
            }
        }
    };
}