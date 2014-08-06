/*
 * main.cpp
 *
 *  Created on: May 17, 2014
 *      Author: brian
 */



#include <iostream>
#include <OpenNet.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#define FUSE_USE_VERSION 26
#include <fuse/fuse.h>
using namespace std;
using namespace OpenNet;


IFilesystem* fs;
static void ConvertToStat(const FileInformation& info, struct stat& output) {

    output.st_ctim.tv_sec = info.created;
    output.st_mtim.tv_sec = time(0);//info.lastModified;
    output.st_size = info.size;
    output.st_mode = info.permissions;

    if(info.isFile){
        output.st_mode = S_IFREG | 0777;
        //output.st_mode = info.permissions;
    }else {
        output.st_mode = S_IFDIR | 0777;
        //output.st_mode = info.permissions;
    }

    output.st_uid = info.owner;
    output.st_gid = info.group;
}
static int FS_getattr(const char* path, struct stat* output) {
	if(std::string(path) == "/") {
        output->st_mode = S_IFDIR | 0777;
        output->st_nlink = 0;
        return 0;
	}
	if(!fs->Exists(path+1)) {
        return -ENOENT;
	}
	FileInformation info;
	fs->Stat(path+1,info);
	ConvertToStat(info,*output);
    output->st_ino = info.inode;
	return 0;
}
static int FS_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
	(void) offset;
    (void) fi;
    std::string realpath = path+1;
    if(fs->Exists(realpath)) {
        //Read root directory
        fs->EnumerateNodes(realpath,[=](const FileInformation& node){
            struct stat output;
            ConvertToStat(node,output);
            std::string fullname = std::string("/")+node.name;
            filler(buf,node.name,&output,0);
            return true;
        });
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        return 0;
    }
    return -ENOENT;
}
static int FS_open(const char *path, struct fuse_file_info *fi) {
	//Open file
	if(!fs->Exists(path+1)) {
		return -ENOENT;
	}
	fi->fh = (size_t)fs->GetStream(path+1);
	return 0;
}
static int FS_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
	OpenNet::Stream* mstr = (OpenNet::Stream*)fi->fh;
	int count = mstr->Read(offset,(unsigned char*)buf,size);
	std::cout<<"Read "<<count<<" bytes.\n";
	return count;
}
static int FS_write(const char *path, const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
	OpenNet::Stream* mstr = (OpenNet::Stream*)fi->fh;
	mstr->Write(offset,(unsigned char*)buf,size);
	return size;
}
static int FS_mknod(const char * name, mode_t permissions, dev_t device) {

	fs->Mknod(name+1,true);
    FileInformation info;
    fs->Stat(name+1, info);
    info.permissions = permissions;
    fs->UpdateMetadata(info);
	return 0;
}
static int FS_mkdir(const char * name, mode_t permissions) {
	fs->Mknod(name+1,false);
	return 0;
}
static int FS_delete(const char* name) {
	if(fs->Exists(name+1)) {
		fs->Delete(name+1);
		return 0;
	}
	return -ENOENT;
}
static int FS_Close(const char* path, struct fuse_file_info* info) {
    OpenNet::Stream* mstr = (OpenNet::Stream*)info->fh;
    delete mstr;
    return 0;
}
static int FS_Rename(const char* oldPath, const char* newPath) {
    if(fs->Exists(oldPath+1)) {
        fs->Rename(oldPath+1, newPath+1);
        return 0;
    }
    return -ENOENT;
}
static int FS_Chmod(const char* name, mode_t mode) {
    if(fs->Exists(name+1)) {
        FileInformation info;
        fs->Stat(name+1, info);
        info.permissions = mode;
        fs->UpdateMetadata(info);
        return 0;
    }
    return -ENOENT;
}
static int FS_Chown(const char* name, uid_t user, gid_t group) {
    if(fs->Exists(name+1)) {
        FileInformation info;
        fs->Stat(name+1, info);
        info.owner = user;
        info.group = group;
        fs->UpdateMetadata(info);
        return 0;
    }
    return -ENOENT;
}
static int FS_StatFS(const char* path, struct statvfs* vfs) {
    vfs->f_namemax = 256;
    vfs->f_bavail = 500000;
    vfs->f_bfree = 500000;
    vfs->f_bsize = 1024*1024*1024;
    vfs->f_blocks = 500;
    vfs->f_frsize = 1024*1024*500;
    return 0;
}

static int FS_Truncate(const char* path, off_t sz) {
    if(fs->Exists(path+1)) {
        auto bot = fs->GetStream(path+1);
        bot->Truncate(sz);
        delete bot;
        return 0;
    }
    return -ENOENT;
}
static int FS_setxattr(const char * path, const char * name, const char * value, size_t size, int flags) {
    if(fs->Exists(path+1)) {
        FileInformation info;
        fs->Stat(path+1, info);
        fs->setxattr(info, name, value, size);
        return 0;
    }
    return -ENOENT;
}
static int FS_getxattr(const char * path, const char * key, char * value, size_t size) {
    if (fs->Exists(path+1)) {
        FileInformation info;
        fs->Stat(path+1, info);
        return fs->getxattr(info, key, value, size);

    }
    return -ENOENT;
}
static int FS_removexattr(const char* path, const char* key) {
    if(fs->Exists(path+1)) {
        FileInformation info;
        fs->Stat(path+1, info);
        fs->removexattr(info, key);
        return 0;
    }
    return -ENOENT;
}
static int FS_listxattr(const char* path, char* buffer, size_t len) {
    if (fs->Exists(path+1)) {
        FileInformation info;
        fs->Stat(path+1, info);
        int retval = fs->listxattr(info, buffer, len);
        return retval;
    }
    return -ENOENT;
}
int main(int argc, char** argv) {



	struct fuse_operations operations;
	memset(&operations,0,sizeof(operations));
	operations.getattr = FS_getattr;
	operations.readdir = FS_readdir;
	operations.open = FS_open;
	operations.read = FS_read;
	operations.write = FS_write;
	operations.mknod = FS_mknod;
	operations.mkdir = FS_mkdir;
	operations.unlink = FS_delete;
    operations.release = FS_Close;
    operations.rename = FS_Rename;
    operations.chmod = FS_Chmod;
    operations.chown = FS_Chown;
    operations.setxattr = FS_setxattr;
    operations.getxattr = FS_getxattr;
    operations.listxattr = FS_listxattr;
    operations.removexattr = FS_removexattr;
    operations.truncate = FS_Truncate;
    operations.statfs = FS_StatFS;
	std::cout<<"LOCALFS mount\n";
	Filesystem localfs;
	Filesystem mountedfs;
	OS_GetFilesystem(&localfs);
	std::cout<<"AWAIT crypto command\n";
	freopen(NULL, "rb", stdin);
	unsigned char key[32];
	memset(key,0,sizeof(key));
	//fread(key,1,32,stdin);
	std::cout<<"Crypto command received -- Mounting filesystem\n";
	//OpenNet_MountFS(&localfs,&mountedfs,(char*)key);
	fs = new ABI_Filesystem(localfs);
	std::cout<<"Mount complete -- Verifying README file.\n";
	fs->EnumerateNodes("",[=](const FileInformation& info){
		std::cout<<info.name<<"\n";
		return true;
	});
	/*if(!fs->Exists("README")) {
		std::cout<<"Created README\n";
		fs->Mknod("README",true);
		if(fs->Exists("README")) {
			FileInformation info;
			fs->Stat("README",info);
			std::cout<<"Creation verified -- length"<<info.size<<"\n";
		}
		OpenNet::Stream* mstr = fs->GetStream("README");
		const char* t = "Welcome to DistFS! Your ultra-secure, distributed network filesystem!";
		mstr->Write(0,(unsigned char*)t,strlen(t));
		delete mstr;
	}
	FileInformation info;
	fs->Stat("README",info);

	OpenNet::Stream* mstr = fs->GetStream("README");
	char mc[1024];
	int count = mstr->Read(0,mc);
	delete mstr;
	*/
    std::cout<<"Filesystem mounted (passing control to FUSE demon).\n";
	int retval = fuse_main(argc,argv,&operations,0);
	//TODO: Disposing of FS should NOT be required to sync filesystem contents
	//this should ideally happen automatically when writing to the master database
	//If this is not called; the database is not ever updated (see the destructor of
	//DistFS)
	//delete fs;
	//localfs.Dispose(localfs.thisptr);
    return retval;
}
