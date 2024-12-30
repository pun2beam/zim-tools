/*
 * Copyright (C) 2019-2020 Matthieu Gautier <mgautier@kymeria.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU  General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <zim/writer/creator.h>
#include <zim/blob.h>
#include <zim/item.h>
#include <zim/archive.h>
#include <list>
#include <algorithm>
#include <sstream>
#include <fstream> // 追加
#include "tools.h"
#include "version.h"

/**
 * A PatchItem. This patch html and css content to remove the namespcae from the links.
 */
class PatchItem : public zim::writer::Item
{
    //article from an existing ZIM file.
    zim::Item item;

  public:
    explicit PatchItem(const zim::Item item):
      item(item)
    {}

    virtual std::string getPath() const
    {
      auto path = item.getPath();
      if (path.length() > 2 && path[1] == '/') {
        path = path.substr(2, std::string::npos);
      }
      return path;
    }

    virtual std::string getTitle() const
    {
        return item.getTitle();
    }

    virtual std::string getMimeType() const
    {
        return item.getMimetype();
    }

    std::unique_ptr<zim::writer::ContentProvider> getContentProvider() const
    {
        auto mimetype = getMimeType();
        if ( mimetype.find("text/html") == std::string::npos
          && mimetype.find("text/css") == std::string::npos) {
            return std::unique_ptr<zim::writer::ContentProvider>(new ItemProvider(item));
        }

        std::string content = item.getData();
        // This is a really poor url rewriting to remove the starting "../<NS>/"
        // and replace the "../../<NS/" by "../" :
        // - Performance may be better
        // - We only fix links in articles in "root" path (`foo.html`) and in one subdirectory (`bar/foo.hmtl`)
        //   Deeper articles are not fixed (`bar/baz/foo.html`).
        // - We may change content starting by `'../A/` even if they are not links
        // - We don't handle links where we go upper in the middle of the link : `../foo/../I/image.png`
        // - ...
        // However, this should patch most of the links in our zim files.
        for (std::string prefix: {"'", "\""}) {
          for (auto ns : {'A','I','J','-'}) {
            replaceStringInPlace(content, prefix+"../../"+ns+"/", prefix+"../");
            replaceStringInPlace(content, prefix+"../"+ns+"/", prefix);
          }
        }
        return std::unique_ptr<zim::writer::ContentProvider>(new zim::writer::StringProvider(content));
    }

  zim::writer::Hints getHints() const {
    return { { zim::writer::HintKeys::FRONT_ARTICLE, guess_is_front_article(item.getMimetype()) } };
  }
};

void printMetaData(const std::string& originFilename, bool withFtIndexFlag, unsigned long nbThreads)
{

  zim::Archive origin(originFilename);
  std::cout << "Metadata:" << std::endl;
  
  for(auto& metakey:origin.getMetadataKeys()) {
    auto metadata = origin.getMetadata(metakey);
    
    if (metakey.find("Illustration_") == 0) {
      // binary value
      std::cout << metakey <<":(binary data)" << std::endl;
    }else{
      std::cout << metakey <<":" << metadata << std::endl;
    }
  }
}
zim::Blob createBlobFromFile(const std::string& filePath) {
    // ファイルをバイナリモードで開く
    std::ifstream file(filePath, std::ios::binary);

    // ファイルが開けなかった場合のエラーハンドリング
    if (!file) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }

    // ファイルサイズを取得
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    // バッファにファイル内容を読み込む
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    // vectorの内部ポインタを使ってstd::shared_ptr<const char>を作成
    std::shared_ptr<const char> constBuffer(buffer.data(), [](const char*) {});

    // Blobオブジェクトを作成して返す
    return zim::Blob(constBuffer, size);
}

void create(const std::string& originFilename, const std::string& outFilename, bool withFtIndexFlag, unsigned long nbThreads,std::map<std::string, std::string> newMetadata)
{
  zim::Archive origin(originFilename);
  zim::writer::Creator zimCreator;
  zimCreator.configVerbose(true)
            // [TODO] Use the correct language
            .configIndexing(withFtIndexFlag, "eng")
            .configClusterSize(2048*1024)
            .configNbWorkers(nbThreads);

  std::cout << "starting zim creation" << std::endl;
  zimCreator.startZimCreation(outFilename);

  auto fromNewNamespace = origin.hasNewNamespaceScheme();

  try {
    auto mainPath = origin.getMainEntry().getItem(true).getPath();
    if (!fromNewNamespace) {
      mainPath = mainPath.substr(2, std::string::npos);
    }
    zimCreator.setMainPath(mainPath);
  } catch(...) {}

  // try {
  //   auto illustration = origin.getIllustrationItem();
  //   zimCreator.addIllustration(48, illustration.getData());
  // } catch(...) {}

  // add original metadata(replace if newMetadata exist)
  for(auto& metakey:origin.getMetadataKeys()) {
    if (metakey == "Counter") {
      // Counter is already added by libzim
      // Illustration is already handled by `addIllustration`
      continue;
    }
    if(metakey.find("Illustration_") == 0){
      zim::Blob IllustBlob;
      if(newMetadata.find(metakey) != newMetadata.end()){
        IllustBlob=createBlobFromFile(newMetadata[metakey]);
      }else{
        auto illustration = origin.getIllustrationItem();
        IllustBlob=illustration.getData();
      }
      zimCreator.addIllustration(48, IllustBlob);
      continue;
    }
    std::string metadata;
    if(newMetadata.find(metakey) != newMetadata.end()){
      metadata = newMetadata[metakey];
    }else{
      metadata = origin.getMetadata(metakey);
    }
    std::cout << "  "<< metakey << ":" << metadata << std::endl;
    auto metaProvider = std::unique_ptr<zim::writer::ContentProvider>(new zim::writer::StringProvider(metadata));
    zimCreator.addMetadata(metakey, std::move(metaProvider), "text/plain");
  }
  // add new metadata(if original medadata not exist)
  for (const auto& pair : newMetadata) {
    std::vector<std::string> existingKeys = origin.getMetadataKeys();
    auto it = std::find(existingKeys.begin(), existingKeys.end(), pair.first);

    if (it == existingKeys.end()) {
        auto metaProvider = std::unique_ptr<zim::writer::ContentProvider>(
            new zim::writer::StringProvider(pair.second)
        );
        std::cout << "  "<< pair.first << ":" << pair.second << std::endl;
        zimCreator.addMetadata(pair.first, std::move(metaProvider), "text/plain");
    }
  }

  for(auto& entry:origin.iterEfficient()) {
    if (fromNewNamespace) {
      //easy, just "copy" the item.
      if (entry.isRedirect()) {
        zimCreator.addRedirection(entry.getPath(), entry.getTitle(), entry.getRedirectEntry().getPath(), {{zim::writer::HintKeys::FRONT_ARTICLE, 1}});
      } else {
        auto tmpItem = std::shared_ptr<zim::writer::Item>(new CopyItem(entry.getItem()));
        zimCreator.addItem(tmpItem);
      }
      continue;
    }

    // We have to adapt the content to drop the namespace.

    auto path = entry.getPath();
    if (path[0] == 'Z' || path[0] == 'X' || path[0] == 'M') {
      // Index is recreated by zimCreator. Do not add it
      continue;
    }

    path = path.substr(2, std::string::npos);
    if (entry.isRedirect()) {
      auto redirectPath = entry.getRedirectEntry().getPath();
      redirectPath = redirectPath.substr(2, std::string::npos);
      zimCreator.addRedirection(path, entry.getTitle(), redirectPath);
    } else {
      auto tmpItem = std::shared_ptr<zim::writer::Item>(new PatchItem(entry.getItem()));
      zimCreator.addItem(tmpItem);
    }

  }
  zimCreator.finishZimCreation();
}
// Parse a single key-value pair string
std::pair<std::string, std::string> parseKeyValuePair(const std::string& pair) {
    size_t colonPos = pair.find(':');
    if (colonPos == std::string::npos) {
        throw std::invalid_argument("Invalid key-value pair format: " + pair);
    }
    std::string key = pair.substr(0, colonPos);
    std::string value = pair.substr(colonPos + 1);
    return {key, value};
}

// Parse the entire input string
std::map<std::string, std::string> parseInputString(const std::string& input) {
    std::map<std::string, std::string> result;

    size_t start = 0, end = 0;
    while ((start = input.find('{', end)) != std::string::npos) {
        end = input.find('}', start);
        if (end == std::string::npos) {
            throw std::invalid_argument("Mismatched braces in input string");
        }

        std::string segment = input.substr(start + 1, end - start - 1);
        auto keyValue = parseKeyValuePair(segment);
        result[keyValue.first] = keyValue.second;
    }

    return result;
}

void usage()
{
    std::cout << "\nzimrecreate recreates a ZIM file from a existing ZIM.\n"
    "\nUsage: zimrecreate ORIGIN_FILE OUTPUT_FILE [Options]"
    "\nOptions:\n"
    "\t-v, --version           print software version\n"
    "\t-mp, --metadataprint    print metadata \n"
    "\t-ms, --metadataset      use metadata insted \n"
    "\t-j, --withoutFTIndex    don't create and add a fulltext index of the content to the ZIM\n"
    "\t-J, --threads <number>  count of threads to utilize (default: 4)\n"
    "\nReturn value:\n"
    "- 0 if no error\n"
    "- -1 if arguments are not valid\n"
    "- -2 if zim creation fails\n";
    return;
}

int main(int argc, char* argv[])
{
    bool withFtIndexFlag = true;
    bool metadataPrintFlag = false;
    unsigned long nbThreads = 4;
    std::map<std::string, std::string> metadata;

    //Parsing arguments
    //There will be only two arguments, so no detailed parsing is required.
    for(int i=0;i<argc;i++)
    {
        if(std::string(argv[i])=="-H" ||
           std::string(argv[i])=="--help" ||
           std::string(argv[i])=="-h")
        {
            usage();
            return 0;
        }

        if(std::string(argv[i])=="--metadataprint" ||
           std::string(argv[i])=="-mp")
        {
            metadataPrintFlag = true;
        }
        
        if(std::string(argv[i])=="--metadataset" ||
           std::string(argv[i])=="-ms")
        {
            if(argc<=i+1)
            {
                std::cout << std::endl << "[ERROR] Not enough Arguments provided" << std::endl;
                usage();
                return -1;
            }
            metadata = parseInputString(argv[i+1]);
        }
        if(std::string(argv[i])=="--version" ||
           std::string(argv[i])=="-v")
        {
            printVersions();
            return 0;
        }
        if(std::string(argv[i])=="--withoutFTIndex" ||
           std::string(argv[i])=="-j")
        {
            withFtIndexFlag = false;
        }

        if(std::string(argv[i])=="-J" ||
           std::string(argv[i])=="--threads")
        {
            if(argc<=i+1)
            {
                std::cout << std::endl << "[ERROR] Not enough Arguments provided" << std::endl;
                usage();
                return -1;
            }
            try
            {
                nbThreads = std::stoul(argv[i+1]);
            }
            catch (...)
            {
                std::cerr << "The number of workers should be a number" << std::endl;
                usage();
                return -1;
            }
        }
    }

    if(argc<3)
    {
        std::cout << std::endl << "[ERROR] Not enough Arguments provided" << std::endl;
        usage();
        return -1;
    }
    std::string originFilename = argv[1];

    if(metadataPrintFlag){
        printMetaData(originFilename, withFtIndexFlag, nbThreads);
        return 0;
    }

    std::string outputFilename = argv[2];
    try
    {
        create(originFilename, outputFilename, withFtIndexFlag, nbThreads, metadata);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return -2;
    }
    return 0;
}
