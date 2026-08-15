// macros/example_loop.C
//
// Example ROOT macro demonstrating the ndlar_light library:
//   - build a Run from an explicit list of subrun files
//   - iterate events sequentially
//   - print event metadata and a few waveform samples per (adc, channel)
//
// Run with:
//   source Setup.sh
//   root -l macros/example_loop.C
//
// (No histogramming here for now - just prints event details to prove
// the library works end-to-end.)

#include "../lib/NDLArLight.hpp"

#include <iostream>

void Dump(string path, int run_number)
{

    try {
        std::cout << "Building run from path: " << path << " run number: " << run_number << "\n";
        // --- 1. Build run and select channels (unchanged) ---
        ndlar_light::Run run(path, run_number);
        run.Print();
//        run.ResetChannels(false);
//        run.SelectChannel(2, 4, true);
//        run.SelectChannel(2, 5, true);

        // --- 2. Baseline calibration (NEW) ---
        // Configure the baseline estimator and calibrator
        ndlar_light::CalibratorConfig cal_cfg;
        cal_cfg.baseline_cfg.window_ticks      = 30;    // ticks per baseline window
        cal_cfg.baseline_cfg.amp_threshold_adc = 120.0;   // max Amp = max-min in window
        cal_cfg.baseline_cfg.asymmetry_factor  = 3.0;   // max AmpBot/AmpTop ratio
        cal_cfg.max_events                     = 3000;  // events to use for calibration
        cal_cfg.fit_range_sigma                = 2.0;   // Gaussian fit range: mean ± 2*RMS

        ndlar_light::BaselineCalibrator calibrator(cal_cfg);
        calibrator.Calibrate(run);   // reads up to 1000 events, fits Gaussians per channel
        calibrator.Print();          // print calibrated baseline table
//        calibrator.Draw();

        // --- 3. WaveAna configuration (NEW) ---
        ndlar_light::WaveAnaConfig wana_cfg;
        wana_cfg.baseline_cfg   = cal_cfg.baseline_cfg; // reuse same baseline settings
        wana_cfg.threshold_adc  = 80.0;                  // hit threshold above baseline

        // --- 4. Build Analysis with a WaveAna factory (NEW) ---
        // The factory captures wana_cfg and the calibrator by reference.
        // Analysis::Loop() will detect WaveAna via dynamic_cast and draw
        // baseline segments (green lines) and hits (red bands + markers).
        ndlar_light::Analysis analysis(run,
            [&wana_cfg, &calibrator](const ndlar_light::Waveform& wf, bool isValid)
            -> std::unique_ptr<ndlar_light::MetaWaveformAna>
            {
                const ndlar_light::ChannelBaseline* fallback =
                    &calibrator.GetBaseline(wf.GetADC(), wf.GetChannel());
                return std::make_unique<ndlar_light::WaveAna>(
                    wf, isValid, wana_cfg, fallback);
            });

        // --- 5. Run the interactive two-row loop ---
        // Loop2() shows, per selected channel, in a 2-row canvas:
        //   top row:    current-event waveform with baseline segments and hits;
        //   bottom row: cumulative individual-hit charge distribution
        //               accumulated across all events displayed so far.
//        analysis.Loop2();
        analysis.process();
        analysis.DumpChargeHistograms(Form("GainHist_%i.root",run_number),"VBR");

        // Alternatively, use the single-row waveform viewer:
        // analysis.Loop();

        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

    } catch (const std::exception& e) {
        std::cerr << "example_loop error: " << e.what() << std::endl;
    }
}


class GainFit : public TF1 {
    public:
    float sigma=270;
    double par[18];
    string parnames[18];
    float gain=1300;
    int NGaus=5;
    string func = "gaus(0)";
    TF1 *f = NULL;
    TF1 *f2 = NULL;
    TF1 *f3 = NULL;
    TGraphErrors *tg=NULL;


    double GainEstimator(double voltage)
    {
        return 581*voltage-30004;
    }
    GainFit(double voltage) {
        gain=GainEstimator(voltage);
        for(int j=1;j<NGaus; j++) func += "+gaus("+to_string(j*3)+")";
        f = new TF1("f",func.c_str(),gain*0.6,gain*(NGaus+1)+0.4*sigma);

        for(int j=0;j<NGaus; j++)
        {
            par[j*3]=600*(NGaus-j);
            par[j*3+1]= gain*(j+1);
            par[j*3+2]=sigma;
            parnames[j*3]="Constant_{"+to_string(j)+"}";
            parnames[j*3+1]="#mu_{"+to_string(j)+"}";
            parnames[j*3+2]="#sigma_{"+to_string(j)+"}";
        }
        f->SetParameters(par);
        for(int j=0;j<NGaus; j++) f->SetParLimits(j*3+1, gain*(j+1)-sigma,gain*(j+1)+sigma);
        for(int j=0;j<NGaus; j++) f->SetParLimits(j*3+2, 0.3*sigma, 3*sigma);
        for(int j=0;j<NGaus; j++) std::cout << "par["<<j*3<<"]="<<par[j*3]<<", par["<<j*3+1<<"]="<<par[j*3+1]<<", par["<<j*3+2<<"]="<<par[j*3+2]<<"\n";
    }
    void Fit(TH1 *h)
    {
        gStyle->SetOptStat(1);
        gStyle->SetOptFit(1);

        h->Fit(f,"RSQEN");
//        h->Fit(f,"ERS");
//        ndlar_light::PauseExecution();
        gain= f->GetParameters()[1];
        sigma=f->GetParameters()[2];;
        f2 = new TF1("f2",func.c_str(),f->GetParameters()[1]*0.6,gain*(NGaus+1)+0.4*sigma);
        f2->SetParameters(f->GetParameters());
    //        for(int j=0;j<NGaus; j++) f2->SetParLimits(j*3+1, gain*(j+1)-sigma,gain*(j+1)+sigma);
        for(int k=0;k<3*NGaus; k++) f2->SetParName(k, parnames[k].c_str());
        h->Fit(f2,"ERS");

        tg = new TGraphErrors();
        ndlar_light::HistName hn = ndlar_light::HistName::Parse(h->GetName());
        int run_number = hn.Run();
        int adc = hn.ADC();
        int ch = hn.Channel();

        tg->SetName(Form("GainVsNPE_%d",run_number));
        tg->SetTitle(Form("Gain vs NPE Run %d ADC %i ch %i; NPE; Hit charge (ADC counts x ticks)",run_number, adc, ch));
        for(int j=0; j<NGaus; j++)
        {
            tg->SetPoint(tg->GetN(),1+j,f2->GetParameters()[j*3+1]);
            tg->SetPointError(tg->GetN()-1,0,f2->GetParErrors()[j*3+1]);
        }
        tg->Draw("AP");
        f3 = new TF1("f3","pol1",0,NGaus+1);
        f3->SetParNames("Intercept","Gain");

        gStyle->SetOptFit(0);

        tg->Fit(f3,"RSE");
        std::cout << "Gain vs NPE Run " << run_number << ": slope = " << f3->GetParameters()[1] << ", intercept = " << f3->GetParameters()[0] << "\n";
        std::cout << "Gain = " << f3->GetParameters()[1] << " +/- " << f3->GetParErrors()[1] << "\n";
    }
    void Draw(TVirtualPad *pad, TH1 *h) {
        pad->cd();
        gPad->SetLogy();
        h->GetXaxis()->SetRangeUser(0,2.5*(gain*(NGaus+1)+0.4*sigma));
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

        gainBox->AddText(Form("Gain = %.3f #pm %.3f", gain, gainError));
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

        tg->SetMarkerStyle(20);
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
};        

void Gain_process() {

/*
    VBRCalibrator vbr_calibrator;
    vbr_calibrator.Load("data/vbr_calibration1.csv");
*/
struct RunTable {
    int run_number;
    float voltage;
    int VGAgain;
};

    RunTable run_table[] = {
        {1121, 51, 120},
        {1122, 51.5,120},
        {1123, 52},
        {1124, 52.5},
        {1125, 53},
        {1126, 53.5},
        {1127, 54.0},
        {1128, 54.5},
        {1129, 55.0},
        {1130, 55.5},
        {1131, 56.0},
        {1132, 56.5},
        {1133, 57},
        {1134, 57.5},
        {1135, 58},
    };
    RunTable run_table2[] = {
        {1126, 53.5},
        {1127, 54.0},
        {1128, 54.5},
        {1129, 55.0},
        {1130, 55.5},
        {1131, 56.0},
        {1132, 56.5},
        {1133, 57},
    };
    if(1)
    {
        TFile file("GainHist.root", "RECREATE");
        file.Close();
        std::string path = "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/";
        for(auto r : run_table2) Dump(path,r.run_number);
    }
    ndlar_light::HistCollection collection;
    collection.Load("GainHist.root");
    int i = 0;
    TCanvas *c = new TCanvas("Gain","Gain",800,600);
    c->DivideSquare(8+1);
    TGraphErrors * tgVBR = new TGraphErrors();
    for (auto r : run_table2) {
        auto h = collection.GetByChannelRun(2, 5,r.run_number)[0];
        c->cd(i+1);
        h->SetLineColor(ndlar_light::MyColors[i]);
        h->SetLineWidth(2);
        ndlar_light::HistName hn = ndlar_light::HistName::Parse(h->GetName());
        h->SetTitle(Form("Run %d - adc %i - ch %i",hn.Run(), hn.ADC(), hn.Channel() ));
        GainFit gainfit(r.voltage);
        gainfit.Fit(h);
        gainfit.Draw(c->cd(i+1),h);
        tgVBR->SetPoint(tgVBR->GetN(),r.voltage,gainfit.f3->GetParameters()[1]);
        tgVBR->SetPointError(tgVBR->GetN()-1,0,gainfit.f3->GetParErrors()[1]);
        i++;
    }
    c->cd(9);
    tgVBR->SetMarkerStyle(20);
    tgVBR->SetLineWidth(2);
    tgVBR->SetMarkerSize(1.2);
    tgVBR->SetTitle("Gain vs Voltage; Voltage (V); Gain (ADC counts x ticks)");

    gStyle->SetOptStat(0);    // no mostrar estadísticas generales
    gStyle->SetOptFit(111);   // chi2/ndf + valores de parámetros + errores    tgVBR->Draw("APE02");
    tgVBR->Draw("APE02");
    TF1 *f = new TF1("f","pol1",50,58);
    f->SetParNames("Intercept","Slope");
    TFitResultPtr result=tgVBR->Fit(f,"RSE");
    // ROOT crea el cuadro durante Update().
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
    const double p0 = f->GetParameter(0);
    const double p1 = f->GetParameter(1);
    const double vbr = -p0 / p1;
    const double dVdp0 = -1.0 / p1;
    const double dVdp1 =  p0 / (p1 * p1);
    const double vbrError = std::sqrt(
        dVdp0 * dVdp0 * result->CovMatrix(0, 0) +
        dVdp1 * dVdp1 * result->CovMatrix(1, 1) +
        2.0 * dVdp0 * dVdp1 * result->CovMatrix(0, 1)
    );
    // A persistent annotation, visually attached below the ROOT fit box.
    auto* vbrBox = new TPaveText(0.15, 0.49, 0.52, 0.57, "NDC");
    vbrBox->SetFillColor(0);
    vbrBox->SetFillStyle(1001);
    vbrBox->SetBorderSize(1);
    vbrBox->SetTextAlign(12);
    vbrBox->SetTextFont(42);
    vbrBox->SetTextSize(0.05);

    vbrBox->AddText(
        Form("V_{BR} = %.3f #pm %.3f V", vbr, vbrError)
    );
    vbrBox->Draw();
    c->Modified();c->Update();

    TCanvas *c2 = new TCanvas("GainVsNPE","GainVsNPE",800,600);
    c2->cd();
    tgVBR->Draw("APE02");
    f->Draw("same");
    ndlar_light::PauseExecution();

}