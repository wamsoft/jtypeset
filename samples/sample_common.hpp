#ifndef TYPESET_SAMPLE_COMMON_HPP
#define TYPESET_SAMPLE_COMMON_HPP

#include <cstdio>
#include <string>
#include <vector>

#include "typeset/backend/pdf_writer.hpp"
#include "typeset/backend/raster.hpp"
#include "typeset/backend/svg_writer.hpp"
#include "typeset/page/page.hpp"

namespace sample {

/// ページ列を PNG（各ページ）/ PDF（全ページ）/ SVG（1 ページ目）へ出す
inline void savePages(const std::vector<typeset::page::Page>& pages, const std::string& stem,
                      float dpi = 144.0f, int maxPng = 2) {
    typeset::backend::RasterRenderer raster;
    typeset::backend::RasterOptions ro;
    ro.dpi = dpi;
    for (size_t i = 0; i < pages.size() && static_cast<int>(i) < maxPng; ++i) {
        const typeset::backend::Bitmap bmp = raster.render(pages[i].dl, ro);
        const std::string path = stem + "_p" + std::to_string(i + 1) + ".png";
        if (typeset::backend::savePng(bmp, path)) {
            std::printf("Saved: %s (%dx%d)\n", path.c_str(), bmp.width, bmp.height);
        }
    }
    typeset::backend::PdfWriter pdf;
    pdf.setTitle(stem);
    for (const auto& p : pages) pdf.addPage(p.dl);
    if (pdf.save(stem + ".pdf")) std::printf("Saved: %s.pdf (%zu pages)\n", stem.c_str(), pages.size());
    for (const auto& w : pdf.warnings()) std::printf("  pdf warning: %s\n", w.c_str());
    if (!pages.empty() && typeset::backend::saveSvg(pages[0].dl, stem + "_p1.svg")) {
        std::printf("Saved: %s_p1.svg\n", stem.c_str());
    }
}

} // namespace sample

#endif
