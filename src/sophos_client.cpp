//
//  sophos_client.cpp
//  sophos
//
//  Created by Raphael Bost on 30/03/2016.
//  Copyright © 2016 Raphael Bost. All rights reserved.
//

#include "sophos_client.hpp"

#include "utils.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <fstream>

#include <grpc/grpc.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

namespace sse {
namespace sophos {


SophosClientRunner::SophosClientRunner(const std::string& address, const std::string& path, size_t setup_size, size_t n_keywords)
{
    std::shared_ptr<grpc::Channel> channel(grpc::CreateChannel(address,
                                                               grpc::InsecureChannelCredentials()));
    stub_ = std::move(sophos::Sophos::NewStub(channel));
                    
    std::string sk_path = path + "/tdp_sk.key";
    std::string master_key_path = path + "/derivation_master.key";
    std::string token_map_path = path + "/tokens.dat";
    
    if (is_directory(path)) {
        // try to initialize everything from this directory
        
        if (!is_file(sk_path)) {
            // error, the secret key file is not there
            throw std::runtime_error("Missing secret key file");
        }
        if (!is_file(master_key_path)) {
            // error, the derivation key file is not there
            throw std::runtime_error("Missing master derivation key file");
        }
        if (!is_directory(token_map_path)) {
            // error, the token map data is not there
            throw std::runtime_error("Missing token data");
        }
        
        std::ifstream sk_in(sk_path.c_str());
        std::ifstream master_key_in(master_key_path.c_str());
        std::stringstream sk_buf, master_key_buf;
        
        sk_buf << sk_in.rdbuf();
        master_key_buf << master_key_in.rdbuf();
        
        client_.reset(new  SophosClient(token_map_path, sk_buf.str(), master_key_buf.str()));

        
    }else if (exists(path)){
        // there should be nothing else than a directory at path, but we found something  ...
        throw std::runtime_error(path + ": not a directory");
    }else{
        // initialize a brand new Sophos client
        
        // start by creating a new directory
        
        if (!create_directory(path, (mode_t)0700)) {
            throw std::runtime_error(path + ": unable to create directory");
        }
        
        client_.reset(new SophosClient(token_map_path,n_keywords));
        
        // write keys to files
        std::ofstream sk_out(sk_path.c_str());
        if (!sk_out.is_open()) {
            throw std::runtime_error(sk_path + ": unable to write the secret key");
        }
        
        sk_out << client_->private_key();
        sk_out.close();
        
        std::ofstream master_key_out(master_key_path.c_str());
        if (!master_key_out.is_open()) {
            throw std::runtime_error(master_key_path + ": unable to write the master derivation key");
        }
        
        master_key_out << client_->master_derivation_key();
        master_key_out.close();

        // send a setup message to the server
        bool success = send_setup(setup_size);
        
        if (!success) {
            throw std::runtime_error("Unsuccessful server setup");
        }
    }
}

bool SophosClientRunner::send_setup(const size_t setup_size) const
{
    grpc::ClientContext context;
    sophos::SetupMessage message;
    google::protobuf::Empty e;

    message.set_setup_size(setup_size);
    message.set_public_key(client_->public_key());
    
    grpc::Status status = stub_->setup(&context, message, &e);

    if (status.ok()) {
        std::cout << "Setup succeeded." << std::endl;
    } else {
        std::cout << "Setup failed: " << std::endl;
        std::cout << status.error_message() << std::endl;
        return false;
    }

    return true;
}
    
const SophosClient& SophosClientRunner::client() const
{
    if (!client_) {
        throw std::logic_error("Invalid state");
    }
    return *client_;
}
    
void SophosClientRunner::search(const std::string& keyword) const
{
    grpc::ClientContext context;
    sophos::SearchRequestMessage message;
    sophos::SearchReply reply;
    
    message = request_to_message(client_->search_request(keyword));
    
    std::unique_ptr<grpc::ClientReader<sophos::SearchReply> > reader( stub_->search(&context, message) );
    while (reader->Read(&reply)) {
        std::cout << "New result: "
        << reply.result() << std::endl;
    }
    grpc::Status status = reader->Finish();
    if (status.ok()) {
        std::cout << "Search succeeded." << std::endl;
    } else {
        std::cout << "Search failed:" << std::endl;
        std::cout << status.error_message() << std::endl;
    }
}

void SophosClientRunner::update(const std::string& keyword, uint64_t index)
{
    grpc::ClientContext context;
    sophos::UpdateRequestMessage message;
    google::protobuf::Empty e;
    
    message = request_to_message(client_->update_request(keyword, index));

    grpc::Status status = stub_->update(&context, message, &e);
    
    if (status.ok()) {
        std::cout << "Update succeeded." << std::endl;
    } else {
        std::cout << "Update failed:" << std::endl;
        std::cout << status.error_message() << std::endl;
    }

}
    
SearchRequestMessage request_to_message(const SearchRequest& req)
{
    SearchRequestMessage mes;
    
    mes.set_add_count(req.add_count);
    mes.set_derivation_key(req.derivation_key);
    mes.set_search_token(req.token.data(), req.token.size());
    
    return mes;
}

UpdateRequestMessage request_to_message(const UpdateRequest& req)
{
    UpdateRequestMessage mes;
    
    mes.set_update_token(req.token.data(), req.token.size());
    mes.set_index(req.index);
    
    return mes;
}


} // namespace sophos
} // namespace sse