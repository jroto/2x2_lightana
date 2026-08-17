#pragma once

#include"TGraphErrors.h"
#include "TF1.h"
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "TFile.h"
#include "TStyle.h"
#include "TPaveText.h"
#include "TPaveStats.h"

using namespace std;

namespace ndlar_light {
    //* This class wil Calibrate a full run, it will create the SPE histograms, and
    // provide gain fits */
    class GainCalibrator
    {
        public:

        static double MultiGaus(double *x, double *par) {
            double val = 0;
            int NGaus = 5; // or pass this in some other way if variable
            for (int i = 0; i < NGaus; i++) {
                double A     = par[3*i];
                double mu    = par[3*i+1];
                double sigma = par[3*i+2];
                val += A * TMath::Exp(-0.5*TMath::Sq((x[0]-mu)/sigma));
            }
            return val;
        }
        /*This class will hold a single Gain fit for a given channel.
        It will contain the fit results and the fit itself and some
        drawing/debugging functions */
        class GainFit {
            public:
            std::string parnames[18];
            float fGain=1300;
            float fSigma=270;
            int NGaus=5;
            std::string func = "gaus(0)";
            TF1 *f1 = NULL;
            TF1 *f2 = NULL;
            TF1 *f3 = NULL;
            TGraphErrors *tg=NULL;
            TGraphErrors *th=NULL;
            bool fitPerformed=false; // true if is succesfull
            bool fitSuccesfull=false; // true if is succesfull
            double fGainError=0;
            double fSigmaError=0;
            TH1 *h=NULL;
            TFitResultPtr fitres;
            double fVoltage=0;

            double GainEstimator(double voltage)
            {
                return 637*voltage-33119;
            }
            double SigmaEstimator(double voltage)
            {
                return 30.5*voltage-1476;
            }
            GainFit(double voltage) {
//                cout << "GainFit constructor called with voltage " << voltage << endl;
                fVoltage=voltage;
                fGain=GainEstimator(voltage);
                fSigma=SigmaEstimator(voltage);
                for(int j=1;j<NGaus; j++) func += "+gaus("+to_string(j*3)+")";
                double min=fGain*0.6;
                double max=fGain*(NGaus+1)+0.4*fSigma;
//                cout << fVoltage << " " << fGain << " " << fSigma << endl;
//                cout << min << " " << max << endl;
                f1 = new TF1(Form("f1"),GainCalibrator::MultiGaus,min,max, NGaus*3);
//                f1 = new TF1(Form("f1"),func.c_str(),fGain*0.6,fGain*(NGaus+1)+0.4*fSigma, NGaus*3);
                double par[18];
 //               PauseExecution();
                for(int j=0;j<NGaus; j++)
                {
                    par[j*3]=600*(NGaus-j);
                    par[j*3+1]= fGain*(j+1);
                    par[j*3+2]=fSigma;
                    parnames[j*3]="Constant_{"+std::to_string(j)+"}";
                    parnames[j*3+1]="#mu_{"+std::to_string(j)+"}";
                    parnames[j*3+2]="#sigma_{"+std::to_string(j)+"}";
                    f1->SetParameter(j*3,par[j*3]);
                    f1->SetParameter(j*3+1,par[j*3+1]);
                    f1->SetParameter(j*3+2,par[j*3+2]);
                }
                for(int j=0;j<NGaus; j++) f1->SetParLimits(j*3+1, fGain*(j+1)-fSigma,fGain*(j+1)+fSigma);
                for(int j=0;j<NGaus; j++) f1->SetParLimits(j*3+2, 0.3*fSigma, 3*fSigma);
//                for(int j=0;j<NGaus; j++) std::cout << "par["<<j*3<<"]="<<par[j*3]<<", par["<<j*3+1<<"]="<<par[j*3+1]<<", par["<<j*3+2<<"]="<<par[j*3+2]<<"\n";
            }
            void Fit(TH1 *hh)
            {
                h=hh;
                gStyle->SetOptStat(1);
                gStyle->SetOptFit(1);
//                h->Draw();
//                h->Fit(f1,"ERS");
//                h->Print();
//                gPad->SetLogy();
                h->Fit(f1,"RSQEN");
//                PauseExecution();
        //        h->Fit(f,"ERS");
        //        ndlar_light::PauseExecution();
                fGain= f1->GetParameters()[1];
                fSigma=f1->GetParameters()[2];
                f2 = new TF1(Form("f2_%s",h->GetName()),GainCalibrator::MultiGaus,f1->GetParameters()[1]*0.6,fGain*(NGaus+1)+0.4*fSigma,NGaus*3);
                f2->SetName(Form("f2_%s",h->GetName()));
                f2->SetParameters(f1->GetParameters());
            //        for(int j=0;j<NGaus; j++) f2->SetParLimits(j*3+1, gain*(j+1)-sigma,gain*(j+1)+sigma);
                for(int k=0;k<3*NGaus; k++) f2->SetParName(k, parnames[k].c_str());
                fitres=h->Fit(f2,"ERSNQ");
                if (!fitres.Get()) {
                    //h->Draw(); PauseExecution();
                    std::cout << h->GetName() << " fit failed." << endl;
                    fitSuccesfull=false;
                    //throw runtime_error("GainCalibrator::GainFit::Fit() Fit failed: no valid TFitResult returned");
                    // handle empty histogram / failed fit
                }
                else
                {
                    fitSuccesfull = (fitres->Status() == 0);
                    fSigma=f2->GetParameters()[2];
                    fSigmaError=f2->GetParErrors()[2];

                    tg = new TGraphErrors();
                    HistName hn = HistName::Parse(h->GetName());
                    int run_number = hn.Run();
                    int adc = hn.ADC();
                    int ch = hn.Channel();

                    tg->SetName(Form("tg_%s",h->GetName()));
                    tg->SetTitle(Form("Gain vs NPE Run %d ADC %i ch %i; NPE; Hit charge (ADC counts x ticks)",run_number, adc, ch));
                    for(int j=0; j<NGaus; j++)
                    {
                        tg->SetPoint(tg->GetN(),1+j,f2->GetParameters()[j*3+1]);
                        tg->SetPointError(tg->GetN()-1,0,f2->GetParErrors()[j*3+1]);
                    }
                    tg->Draw("AP");
                    f3 = new TF1(Form("f3_%s",h->GetName()),"pol1",0,NGaus+1);
                    f3->SetParNames("Intercept","Gain");
                    gStyle->SetOptFit(0);
                    tg->Fit(f3,"RSENQ");
                    FillVarsFromFit();
                    std::cout << "Gain vs NPE Run " << run_number << ": slope = " << f3->GetParameters()[1] << ", intercept = " << f3->GetParameters()[0] << "\n";
                    std::cout << "Gain = " << fGain << " +/- " << fGainError << "\n";
                }
                fitPerformed=true;

            }
            void FillVarsFromFit()
            {
                fGain=f3->GetParameters()[1];
                fGainError=f3->GetParErrors()[1];
                fSigma=f2->GetParameters()[2];
                fSigmaError=f2->GetParErrors()[2];
            }
            void Draw(TVirtualPad *pad) {
                pad->cd();
                gPad->SetLogy();
                h->GetXaxis()->SetRangeUser(0,2.5*(fGain*(NGaus+1)+0.4*fSigma));
                h->Draw("hist");
                f2->Draw("same");
                pad->Modified(); pad->Update();

                TPaveText* gainBox = new TPaveText(0.58, 0.32, 0.89, 0.50, "NDC");
                gainBox->SetFillColor(0);       // white
                gainBox->SetFillStyle(1001);    // opaque, covers the plot behind it
                gainBox->SetBorderSize(1);
                gainBox->SetTextAlign(12);      // left-aligned, vertically centred
                gainBox->SetTextFont(42);
                gainBox->SetTextSize(0.04);
                // Read the result from the already-completed fit.
                const double gain = f3->GetParameter(1);
                const double gainError = f3->GetParError(1);

                gainBox->AddText(Form("Gain = %.1f #pm %.1f", gain, gainError));
                gainBox->Draw();

                pad->Modified();
                pad->Update();
                // Move the fit box to the top-left
                TPaveStats *st = (TPaveStats*)h->FindObject("stats");
                if (st) {
                    st->SetOptStat(0);
                    st->SetOptFit(100);   // chi2/ndf only        h->Draw("hist");
                    st->SetX1NDC(0.12);  st->SetX2NDC(0.40);
                    st->SetY1NDC(0.75);  st->SetY2NDC(0.92);
                    st->Draw();
                }

        //        ndlar_light::PauseExecution();
                TPad *inset = new TPad("inset", "inset", 0.50, 0.50, 0.95, 0.95);
                inset->SetFillColor(0);       // white background
                inset->SetBorderMode(0);      // no border style
                inset->SetBorderSize(1);      // thin border
                inset->Draw();
                inset->cd();                  // switch drawing to the inset

                tg->SetMarkerStyle(7);
                tg->SetLineWidth(2);
                tg->SetMarkerSize(1.2);
                tg->Draw("APE02");
                f3->Draw("same");
                inset->Update();
                TPaveStats *st2 = new TPaveStats(0.1, 0.65, 0.60, 0.92, "NDC");
                st2->SetName("stats");
                st2->SetFillColor(0);
                st2->SetBorderSize(1);
                st2->SetTextAlign(12);

                // Add chi2/ndf line
                st2->AddText(Form("#chi^{2} / ndf = %.3f / %d",
                f3->GetChisquare(), f3->GetNDF()));

                // Add one line per parameter
                for (int i = 0; i < f3->GetNpar(); ++i) {
                    st2->AddText(Form("%s = %.4g #pm %.4g",
                    f3->GetParName(i),
                    f3->GetParameter(i),
                    f3->GetParError(i)));
                }
                st2->SetFillColor(0);
                st2->SetBorderSize(1);
                tg->GetListOfFunctions()->Add(st2);
                st2->Draw();        // Move the fit box to the top-left
                inset->Modified(); inset->Update();
            }   
        }; //end of GainFit class

        Run *fRun=NULL;
        std::vector<GainFit> fGainFits; // one per channel
        std::vector<std::pair<int, int>> fSelectedChannels; // vector of selected channels (adc, ch)
        double fVoltage=0.0;
        std::string kGainFitsFile;
        std::string kGainHistFile;
        bool Status=false; //True if the fGainFits are available.
        
        GainCalibrator(Run* r, double v) : fRun(r), fVoltage (v) {
            std::cout << "GainCalibrator on run "<< fRun->RunNumber()<<"\n";
            fSelectedChannels = fRun->GetSelectedChannels();
//            fRun->Print();
            kGainFitsFile= Form("GainFits_%i.root",fRun->RunNumber());
            kGainHistFile= Form("GainHist_%i.root",fRun->RunNumber());


            //Run hitfinder, create the SPE histograms and dump them to a file for
            // all selected channels.
            // 1. ProcessSPEHist(); -> Output is a file
            //Gain fits for selected channels
            // 2. PerformGainFits(); -> Output is a file too
        }
        void Print()
        {
            std::cout << "GainCalibrator for run " << fRun->RunNumber() << ", voltage " << fVoltage << "\n";
            std::cout << fSelectedChannels.size() << " selected channels\n";
//            for (const auto& channel : fSelectedChannels)
//            {
//                std::cout << "ADC: " << channel.first << ", Channel: " << channel.second << "\n";
//            }
            std::cout << "Gain fits: " << fGainFits.size() <<"\n";
            for (const auto& gainfit : fGainFits)
            {
                ndlar_light::HistName hn = ndlar_light::HistName::Parse(gainfit.h->GetName());
                std::cout << "ADC: " << hn.ADC() << ", Channel: " << hn.Channel() 
                          << ", Gain: " << gainfit.fGain 
                          << ", Gain Error: " << gainfit.fGainError 
                          << ", Fit Performed: " << gainfit.fitPerformed 
                          << ", Fit Successful: " << gainfit.fitSuccesfull 
                          << "\n";
            }
        }
        GainFit & GetGainFitPerChannel(int adc, int ch)
        {
            for (auto &gf : fGainFits)
            {
                ndlar_light::HistName hn = ndlar_light::HistName::Parse(gf.h->GetName());
                if (hn.ADC() == adc && hn.Channel() == ch)
                {
                    return gf;
                }
            }
            throw std::runtime_error(Form("GainCalibrator::GetGainFitPerChannel: Gain fit not found for ADC %d, Channel %d", adc, ch));
        }
        void Process()
        {

//            ProcessSPEHist();
            PerformGainFits();
        }
        void PerformGainFits()
        {
            std::cout << "GainCalibrator::PerformGainFits start\n";

            if (gSystem->AccessPathName(kGainHistFile.c_str())) {
                std::cout << "GainCalibrator::PerformGainFits File does not exist: " << kGainHistFile 
                 << ", processing SPE to create it...\n";
                ProcessSPEHist();
            }
            ndlar_light::HistCollection collection;
            collection.Load(kGainHistFile);
            fSelectedChannels = fRun->GetSelectedChannels();
            fRun->Print();
//            fRun->fChannelMap.Print();
            for (const auto& channel : fSelectedChannels)
            {
                const int adc = channel.first;
                const int ch  = channel.second;
//                cout << "GainCalibrator::PerformGainFits: Processing ADC " << adc << ", Channel " << ch << "\n";
                if(!fRun) {
                    std::cerr << "GainCalibrator::PerformGainFits: Error, Run not set\n";
                }
                auto h = collection.GetByChannelRun(adc, ch,fRun->RunNumber())[0];
                h->Print();
                if (!h) {
                    std::cerr << "GainCalibrator::PerformGainFits: Error, histogram not found for ADC " << adc << ", Channel " << ch << "\n";
                    continue;
                }
                ndlar_light::HistName hn = ndlar_light::HistName::Parse(h->GetName());
                h->SetTitle(Form("Run %d - adc %i - ch %i",hn.Run(), hn.ADC(), hn.Channel() ));
                GainFit gainfit(fVoltage);
                gainfit.Fit(h);
                fGainFits.push_back(gainfit);
            }  
            DumpGainFits();
            Status=true;
        }
        void DumpGainFits()
        {
            TFile *f = new TFile(kGainFitsFile.c_str(),"RECREATE");
            HistCollection collection;
            for (const auto& g : fGainFits)
            {
                collection.Add(HistName::Parse(g.h->GetName()) ,*g.h);
                g.f2->Write();
                g.f3->Write();
                g.tg->Write();
                g.fitres->Write(Form("fitres_%s",g.h->GetName()));
            }
            f->Close();
            collection.Dump(kGainFitsFile,"UPDATE");
            std::cout << "GainCalibrator::DumpGainFits completed, gain fits saved to " << kGainFitsFile << "\n";
        }
        void LoadGainFits()
        {
            TFile *file = new TFile(kGainFitsFile.c_str(),"READ");
            if (!file || file->IsZombie()) {
                std::cout << "GainCalibrator::LoadGainFits: No gains in file or zombie " << kGainFitsFile << "\n";
                std::cout << "GainCalibrator::LoadGainFits: Performing gain fits instead\n";
                PerformGainFits();
                return;
            }
            std::cout << "GainCalibrator::LoadGainFits starts\n";
            ndlar_light::HistCollection collection;
            collection.Load(kGainHistFile.c_str());
            fGainFits.clear();
            fSelectedChannels = fRun->GetSelectedChannels();
            for (auto const& channel : fSelectedChannels)
            {
                auto adc = channel.first;
                auto ch = channel.second;
                std::cout <<"====" <<adc << " "<< ch <<  " "<< fRun->RunNumber() <<"\n";
                TH1 *h = (TH1*) collection.GetByChannelRun(adc, ch,fRun->RunNumber())[0];
                TF1 *f2 = (TF1*)file->Get(Form("f2_%s",h->GetName()));
                TF1 *f3 = (TF1*)file->Get(Form("f3_%s",h->GetName()));
                TGraphErrors *tg = (TGraphErrors*)file->Get(Form("tg_%s",h->GetName()));
                TFitResult* fitresFromFile = dynamic_cast<TFitResult*>(file->Get(Form("fitres_%s", h->GetName())));

                if(!h || !f2 || !tg|| !f3 || !fitresFromFile) {
                    throw std::runtime_error("GainCalibrator::LoadGainFits: Error, missing objects for ADC "
                         + std::to_string(adc) + ", Channel " + std::to_string(ch)
                         + ", probably file "+kGainFitsFile+" is corrupted\n");
                }

                GainFit gainfit(fVoltage);
                gainfit.h=dynamic_cast<TH1*>(h->Clone());
                if (fitresFromFile != nullptr) {
                    // Make an independent copy which survives file->Close().
                    gainfit.fitres = TFitResultPtr(
                        new TFitResult(*fitresFromFile)
                    );
                    gainfit.fitPerformed=true;
                    gainfit.fitSuccesfull =
                        gainfit.fitres->Status() == 0 &&
                        gainfit.fitres->IsValid();
                    gainfit.f2=dynamic_cast<TF1*>(f2->Clone());
                    gainfit.f3=dynamic_cast<TF1*>(f3->Clone());
                    gainfit.tg=dynamic_cast<TGraphErrors*>(tg->Clone());
//                    gainfit.fitres=*dynamic_cast<TFitResultPtr*>(fitresFromFile->Clone());
                    gainfit.FillVarsFromFit();
                } else {
                    gainfit.fitPerformed = false;
                    gainfit.fitSuccesfull = false;
                }
//                TFitResultPtr fitres = (TFitResultPtr)file->Get(Form("fitres_%s",h->GetName()));
//                gainfit.fitres=*dynamic_cast<TFitResultPtr*>(fitres->Clone());
//                gainfit.fitPerformed=true;
//                gainfit.fitSuccesfull = (fitres->Status() == 0);
                fGainFits.push_back(gainfit);
            }
            file->Close();
            Status=true;
            std::cout << "GainCalibrator::LoadGainFits completed, " << fGainFits.size() << " gain fits loaded\n";
        }
        void ProcessSPEHist()
        {
            try {
                std::cout << "GainCalibrator::ProcessSPEHist start for run " << fRun->RunNumber() << "\n";
                // --- 1. Build run and select channels (unchanged) ---
        //        fRun->ResetChannels(false);
        //        fRun->SelectChannel(2, 4, true);
        //        fRun->SelectChannel(2, 5, true);

                // --- 2. Baseline calibration (NEW) ---
                // Configure the baseline estimator and calibrator
                CalibratorConfig cal_cfg;
                cal_cfg.baseline_cfg.window_ticks      = 30;    // ticks per baseline window
                cal_cfg.baseline_cfg.amp_threshold_adc = 120.0;   // max Amp = max-min in window
                cal_cfg.baseline_cfg.asymmetry_factor  = 3.0;   // max AmpBot/AmpTop ratio
                cal_cfg.max_events                     = 3000;  // events to use for calibration
                cal_cfg.fit_range_sigma                = 2.0;   // Gaussian fit range: mean ± 2*RMS

                std::cout << "GainCalibrator::ProcessSPEHist::BaselineCalibrator start\n";

                BaselineCalibrator calibrator(cal_cfg);
                calibrator.Calibrate(*fRun);   // reads up to 1000 events, fits Gaussians per channel
                calibrator.Print();          // print calibrated baseline table
        //        calibrator.Draw();

                // --- 3. WaveAna configuration (NEW) ---
                WaveAnaConfig wana_cfg;
                wana_cfg.baseline_cfg   = cal_cfg.baseline_cfg; // reuse same baseline settings
                wana_cfg.threshold_adc  = 80.0;                  // hit threshold above baseline

                // --- 4. Build Analysis with a WaveAna factory (NEW) ---
                // The factory captures wana_cfg and the calibrator by reference.
                // Analysis::Loop() will detect WaveAna via dynamic_cast and draw
                // baseline segments (green lines) and hits (red bands + markers).
                std::cout << "GainCalibrator::ProcessSPEHist::analysis start\n";
                Analysis analysis(*fRun,
                    [&wana_cfg, &calibrator](const Waveform& wf, bool isValid)
                    -> std::unique_ptr<MetaWaveformAna>
                    {
                        const ChannelBaseline* fallback =
                            &calibrator.GetBaseline(wf.GetADC(), wf.GetChannel());
                        return std::make_unique<WaveAna>(
                            wf, isValid, wana_cfg, fallback);
                    });

                // --- 5. Run the interactive two-row loop ---
                // Loop2() shows, per selected channel, in a 2-row canvas:
                //   top row:    current-event waveform with baseline segments and hits;
                //   bottom row: cumulative individual-hit charge distribution
                //               accumulated across all events displayed so far.
        //        analysis.Loop2();
                std::cout << "GainCalibrator::ProcessSPEHist::analysis::process\n";
                analysis.process(2000);
                DumpChargeHistograms(analysis,kGainHistFile,"VBR");

                // Alternatively, use the single-row waveform viewer:
                // analysis.Loop();

                std::cout << "Run has " << fRun->NumSubruns() << " subrun(s), "
                        << fRun->TotalEvents() << " total events.\n";

            } catch (const std::exception& e) {
                std::cerr << "GainCalibrator::ProcessSPEHist error: " << e.what() << std::endl;
            }
        }

        void DumpChargeHistograms(Analysis& analysis,
            const std::string& filename,
            const std::string& tag) const
        {
            if (analysis.GetEvents().empty()) {
                throw std::logic_error(
                    "Analysis::DumpChargeHistograms: no processed events; "
                    "call Analysis::process() before dumping charge histograms");
            }

            // Take one stable snapshot. Each selected channel gets exactly one
            // output histogram, including channels for which no hit is found.
            const std::vector<std::pair<int, int>> selectedChannels =
                fRun->GetSelectedChannels();

            if (selectedChannels.empty()) {
                throw std::logic_error(
                    "Analysis::DumpChargeHistograms: no channels are selected");
            }

            constexpr int    kChargeHistBins = 200;
            constexpr double kChargeHistMin  = 0.0;
            constexpr double kChargeHistMax  = 30000.0;

            struct ChargeHistogram {
                HistName metadata;
                std::unique_ptr<TH1F> histogram;
            };

            std::vector<ChargeHistogram> chargeHistograms;
            chargeHistograms.reserve(selectedChannels.size());

            // Create every selected-channel histogram before examining events.
            for (const auto& channel : selectedChannels) {
                const int adc = channel.first;
                const int ch  = channel.second;

                const HistName metadata(
                    adc,
                    ch,
                    fRun->RunNumber(),
                    fRun->StartTimeUnixSeconds(),
                    "charge",
                    tag);

                // This temporary ROOT name is irrelevant: HistCollection::Add()
                // replaces it with metadata.ToString() when storing the clone.
                const std::string temporaryName =
                    "tmp_charge_adc" + std::to_string(adc) +
                    "_ch" + std::to_string(ch);

                const std::string title =
                    "ADC " + std::to_string(adc) +
                    " / CH " + std::to_string(ch) +
                    " individual hit charge;"
                    "Hit charge (ADC counts #times ticks);Hits";

                std::unique_ptr<TH1F> histogram(new TH1F(
                    temporaryName.c_str(),
                    title.c_str(),
                    kChargeHistBins,
                    kChargeHistMin,
                    kChargeHistMax));

                // The temporary histogram is owned here, not by gDirectory.
                histogram->SetDirectory(nullptr);
                histogram->SetLineColor(kBlue + 1);

                chargeHistograms.push_back(
                    ChargeHistogram{metadata, std::move(histogram)});
            }

            // Fill from the already processed EventAna objects only.
            for (const EventAna& eventAna : analysis.GetEvents()) {
                for (std::size_t i = 0; i < selectedChannels.size(); ++i) {
                    const int adc = selectedChannels[i].first;
                    const int ch  = selectedChannels[i].second;

                    // A selected channel can be invalid in an individual event.
                    if (!eventAna.Meta().IsValid(adc, ch)) {
                        continue;
                    }

                    const MetaWaveformAna& waveformAna =
                        eventAna.GetWaveformAna(adc, ch);

                    // Other MetaWaveformAna implementations do not expose
                    // individual hits. Leave their selected-channel histogram
                    // empty rather than failing.
                    const WaveAna* waveAna =
                        dynamic_cast<const WaveAna*>(&waveformAna);

                    if (waveAna == nullptr) {
                        continue;
                    }

                    // Fill exactly once per individual Hit::charge.
                    for (const Hit& hit : waveAna->Hits()) {
                        chargeHistograms[i].histogram->Fill(hit.charge);
                    }
                }
            }

            // HistCollection takes an independent final clone of each completed
            // histogram and assigns its canonical HistName ROOT object name.
            HistCollection collection;

            for (const ChargeHistogram& item : chargeHistograms) {
                collection.Add(item.metadata, *item.histogram);
            }
            collection.Dump(filename,"RECREATE");
            std::cout << "Analysis::DumpChargeHistograms: wrote "
                    << collection.Size()
                    << " charge histogram(s) to '"
                    << filename
                    << "'\n";
        }


    }; //end of GainCalibrator class
}       
