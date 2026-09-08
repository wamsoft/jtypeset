#ifndef TYPESET_OBJ_OBJECT_HPP
#define TYPESET_OBJ_OBJECT_HPP

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glyphware/Face.h>

#include "typeset/dl/display_list.hpp"
#include "typeset/writing_mode.hpp"

/**
 * obj — 外部生成オブジェクト（数式・グラフ・図など）の差し込み口
 *
 * 本文側は「ハンドラ名＋ソース文字列＋パラメータ」だけを書く。組版時に FlowLayouter が
 * 登録済みのハンドラを呼び、返ってきた箱の大きさとベースラインで配置し、描画命令
 * （表示リストの要素）をそのまま出力へ流す。本体は数式や図の知識を持たない。
 *
 * ハンドラは 2 通り:
 *  - 関数（ObjectHandler）: 表示リストを直接返せる。GlyphRun で返せば PDF でも字として埋め込まれる
 *  - 外部コマンド: 要求を JSON ファイルに書き、`command <request.json>` の標準出力（SVG）を
 *    読み込む（obj/svg_import）。メトリクスは SVG の width/height と、あれば
 *    `<!-- typeset baseline="12.3" -->` のコメント（上端からベースラインまで、pt）
 *
 * 結果は (ハンドラ, ソース, パラメータ, サイズ, 行内か) でキャッシュする。目次・参照の多パスで
 * 同じ式を何度も組むため。
 */
namespace typeset::obj {

struct ObjectRequest {
    std::string handler;
    std::u16string source;
    std::map<std::string, std::string> params;
    Pt maxInline = 0.0f;        ///< 置ける行方向の最大（段の幅など。0 = 制限なし）
    Pt maxBlock = 0.0f;         ///< 行送り方向の最大（0 = 制限なし）
    Pt fontSize = 10.0f;        ///< 周囲の本文のサイズ
    WritingMode writingMode = WritingMode::HorizontalTb;
    bool inlineContext = false; ///< 行内（true）か別行立て（false）か
};

struct ObjectResult {
    Size size;                  ///< 箱の大きさ（pt）
    Pt baseline = 0.0f;         ///< 上端からベースラインまで（行内で本文のベースラインに揃える。0 なら中心揃え）
    bool hasBaseline = false;
    std::vector<dl::Item> items;    ///< オブジェクト座標系（左上原点、y-down、pt）
    std::vector<std::shared_ptr<glyphware::Face>> fonts;   ///< items 内の GlyphRun が使う face（寿命保持）
    std::string error;          ///< 空なら成功

    bool ok() const { return error.empty() && size.w > 0.0f && size.h > 0.0f; }
};

using ObjectHandler = std::function<ObjectResult(const ObjectRequest&)>;

class ObjectRegistry {
public:
    ObjectRegistry();
    ~ObjectRegistry();

    /// 関数ハンドラを登録する
    void add(const std::string& name, ObjectHandler handler);

    /**
     * 外部コマンドを登録する。`commandLine <request.json>` を実行し、標準出力の SVG を読む
     * @param workDir 作業ディレクトリ（空なら現在地）
     */
    void addCommand(const std::string& name, const std::string& commandLine,
                    const std::string& workDir = std::string());

    bool has(const std::string& name) const;

    /// ハンドラを呼ぶ（キャッシュあり）。未登録なら error 付きの結果
    std::shared_ptr<const ObjectResult> render(const ObjectRequest& req);

    void clearCache();
    size_t cacheSize() const;

    /// 直近のエラー（未登録・失敗）
    const std::vector<std::string>& errors() const { return errors_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> errors_;
};

/// 要求を JSON にする（外部コマンドの入力。UTF-8）
std::string requestToJson(const ObjectRequest& req);

/// 表示リスト要素を ObjectResult に詰める補助
ObjectResult makeResult(Size size, Pt baseline, std::vector<dl::Item> items);

} // namespace typeset::obj

#endif // TYPESET_OBJ_OBJECT_HPP
