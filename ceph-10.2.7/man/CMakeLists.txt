install(FILES
	ceph-osd.8
	ceph-mds.8
	ceph-mon.8
	ceph-syn.8
	crushtool.8
	osdmaptool.8
	monmaptool.8
	ceph-conf.8
	ceph-run.8
	ceph.8
	mount.ceph.8
	ceph-create-keys.8
	radosgw.8
	radosgw-admin.8
	ceph-authtool.8
	rados.8
	librados-config.8
	ceph-clsinfo.8
	ceph-debugpack.8
	cephfs.8
	ceph-dencoder.8
	ceph-rest-api.8
	ceph-post-file.8
  DESTINATION ${CEPH_MAN_DIR}/man8)

if(HAVE_LIBFUSE)
  install(FILES
	ceph-fuse.8
	rbd-fuse.8
	DESTINATION ${CEPH_MAN_DIR}/man8)
endif()

if(WITH_RBD)
  install(FILES
	ceph-rbdnamer.8
	rbd-nbd.8
	rbd-replay-prep.8
	rbd-replay.8
	rbdmap.8
	rbd.8
	DESTINATION ${CEPH_MAN_DIR}/man8)
endif()
